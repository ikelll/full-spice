/*
   Copyright (C) 2011, 2012 Red Hat, Inc.

   Red Hat Authors:
   Hans de Goede <hdegoede@redhat.com>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, see <http://www.gnu.org/licenses/>.
*/

#include "config.h"

#include <glib-object.h>

#ifdef USE_USBREDIR
#include <errno.h>
#include <libusb-1.0/libusb.h>

#ifdef G_OS_WIN32
#include <windows.h>
#include "usbdk_api.h"
#endif

#include "channel-usbredir-priv.h"
#include "usb-device-cd.h"
#endif

#include "spice-session-priv.h"
#include "spice-client.h"
#include "spice-marshal.h"
#include "usb-device-manager-priv.h"

#include <glib/gi18n-lib.h>

#ifndef G_OS_WIN32 /* Linux -- device id is bus.addr */
#define DEV_ID_FMT "at %u.%u"
#else /* Windows -- device id is vid:pid */
#define DEV_ID_FMT "0x%04x:0x%04x"
#endif

/**
 * SECTION:usb-device-manager
 * @short_description: USB device management
 * @title: Spice USB Manager
 * @section_id:
 * @see_also:
 * @stability: Stable
 * @include: spice-client.h
 *
 * #SpiceUsbDeviceManager monitors USB redirection channels and USB
 * devices plugging/unplugging. If #SpiceUsbDeviceManager:auto-connect
 * is set to %TRUE, it will automatically connect newly plugged USB
 * devices to available channels.
 *
 * There should always be a 1:1 relation between #SpiceUsbDeviceManager objects
 * and #SpiceSession objects. Therefor there is no
 * spice_usb_device_manager_new, instead there is
 * spice_usb_device_manager_get() which ensures this 1:1 relation.
 */

G_STATIC_ASSERT(sizeof(SpiceUsbDeviceManagerClass) == sizeof(GObjectClass) + 14 * sizeof(gpointer));

enum {
    PROP_0,
    PROP_SESSION,
    PROP_AUTO_CONNECT,
    PROP_AUTO_CONNECT_FILTER,
    PROP_REDIRECT_ON_CONNECT,
    PROP_POLICY_FILTER,
    PROP_FREE_CHANNELS,
};

enum
{
    DEVICE_ADDED,
    DEVICE_REMOVED,
    AUTO_CONNECT_FAILED,
    DEVICE_ERROR,
    LAST_SIGNAL,
};

struct _SpiceUsbDeviceManagerPrivate {
    SpiceSession *session;
    gboolean auto_connect;
    gchar *auto_connect_filter;
    gchar *redirect_on_connect;
    gchar *policy_filter;
#ifdef USE_USBREDIR
    SpiceUsbBackend *context;
    struct usbredirfilter_rule *auto_conn_filter_rules;
    struct usbredirfilter_rule *redirect_on_connect_rules;
    struct usbredirfilter_rule *policy_filter_rules;
    int auto_conn_filter_rules_count;
    int redirect_on_connect_rules_count;
    int policy_filter_rules_count;
    gboolean redirecting;
#ifdef G_OS_WIN32
    usbdk_api_wrapper *usbdk_api;
    HANDLE usbdk_hider_handle;
#endif
    GPtrArray *devices;
    GPtrArray *channels;
#endif
};

enum {
    SPICE_USB_DEVICE_STATE_NONE = 0, /* this is also DISCONNECTED */
    SPICE_USB_DEVICE_STATE_CONNECTING,
    SPICE_USB_DEVICE_STATE_CONNECTED,
    SPICE_USB_DEVICE_STATE_DISCONNECTING,
    SPICE_USB_DEVICE_STATE_INSTALLING,
    SPICE_USB_DEVICE_STATE_UNINSTALLING,
    SPICE_USB_DEVICE_STATE_INSTALLED,
    SPICE_USB_DEVICE_STATE_MAX
};

#ifdef USE_USBREDIR

static void channel_new(SpiceSession *session, SpiceChannel *channel,
                        gpointer user_data);
static void channel_destroy(SpiceSession *session, SpiceChannel *channel,
                            gpointer user_data);
static void channel_event(SpiceChannel *channel, SpiceChannelEvent event,
                          gpointer user_data);
static void spice_usb_device_manager_hotplug_cb(void *user_data,
                                                SpiceUsbDevice *dev,
                                                gboolean added);
static void spice_usb_device_manager_check_redir_on_connect(SpiceUsbDeviceManager *manager,
                                                            SpiceChannel *channel);

static SpiceUsbDevice *spice_usb_device_new(SpiceUsbDevice *bdev);
static SpiceUsbDevice *spice_usb_device_ref(SpiceUsbDevice *device);
static void spice_usb_device_unref(SpiceUsbDevice *device);

#ifdef G_OS_WIN32
static void _usbdk_hider_update(SpiceUsbDeviceManager *manager);
static void _usbdk_hider_clear(SpiceUsbDeviceManager *manager);
#endif

static gboolean spice_usb_manager_device_equal_bdev(SpiceUsbDeviceManager *manager,
                                                    SpiceUsbDevice *device,
                                                    SpiceUsbDevice *bdev);

static void
_spice_usb_device_manager_connect_device_async(SpiceUsbDeviceManager *manager,
                                               SpiceUsbDevice *device,
                                               GCancellable *cancellable,
                                               GAsyncReadyCallback callback,
                                               gpointer user_data);

static
void _connect_device_async_cb(GObject *gobject,
                              GAsyncResult *channel_res,
                              gpointer user_data);
static
void disconnect_device_sync(SpiceUsbDeviceManager *manager,
                            SpiceUsbDevice *device);

G_DEFINE_BOXED_TYPE(SpiceUsbDevice, spice_usb_device,
                    (GBoxedCopyFunc)spice_usb_device_ref,
                    (GBoxedFreeFunc)spice_usb_device_unref)

static void
_set_redirecting(SpiceUsbDeviceManager *manager, gboolean is_redirecting)
{
    manager->priv->redirecting = is_redirecting;
}

#else
G_DEFINE_BOXED_TYPE(SpiceUsbDevice, spice_usb_device, g_object_ref, g_object_unref)
#endif

/**
 * spice_usb_device_manager_is_redirecting:
 * @manager: the #SpiceUsbDeviceManager manager
 *
 * Checks whether a device is being redirected
 *
 * Returns: %TRUE if device redirection negotiation flow is in progress
 *
 * Since: 0.32
 */
gboolean spice_usb_device_manager_is_redirecting(SpiceUsbDeviceManager *manager)
{
#ifndef USE_USBREDIR
    return FALSE;
#else
    return manager->priv->redirecting;
#endif
}

static void spice_usb_device_manager_initable_iface_init(GInitableIface *iface);

static guint signals[LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE_WITH_CODE(SpiceUsbDeviceManager, spice_usb_device_manager, G_TYPE_OBJECT,
     G_ADD_PRIVATE(SpiceUsbDeviceManager)
     G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, spice_usb_device_manager_initable_iface_init))

static void spice_usb_device_manager_init(SpiceUsbDeviceManager *manager)
{
    SpiceUsbDeviceManagerPrivate *priv;

    priv = spice_usb_device_manager_get_instance_private(manager);
    manager->priv = priv;

#if defined(G_OS_WIN32) && defined(USE_USBREDIR)
    if (usbdk_is_driver_installed()) {
        priv->usbdk_api = usbdk_api_load();
    } else {
        g_warning("UsbDk driver is not installed");
    }
#endif
#ifdef USE_USBREDIR
    priv->channels = g_ptr_array_new();
    priv->devices  = g_ptr_array_new_with_free_func((GDestroyNotify)
                                                    spice_usb_device_unref);
#endif
}

static gboolean spice_usb_device_manager_initable_init(GInitable  *initable,
                                                       GCancellable  *cancellable,
                                                       GError        **err)
{
#ifndef USE_USBREDIR
    g_set_error_literal(err, SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                        _("USB redirection support not compiled in"));
    return FALSE;
#else
    SpiceUsbDeviceManager *manager = SPICE_USB_DEVICE_MANAGER(initable);
    SpiceUsbDeviceManagerPrivate *priv = manager->priv;
    GList *list;
    GList *it;

    /* Initialize libusb */
    priv->context = spice_usb_backend_new(err);
    if (!priv->context) {
        return FALSE;
    }

    /* Start listening for usb devices plug / unplug */
    if (!spice_usb_backend_register_hotplug(priv->context, manager,
                                            spice_usb_device_manager_hotplug_cb,
                                            err)) {
        return FALSE;
    }

    /* Start listening for usb channels connect/disconnect */
    spice_g_signal_connect_object(priv->session, "channel-new", G_CALLBACK(channel_new), manager, G_CONNECT_AFTER);
    g_signal_connect(priv->session, "channel-destroy",
                     G_CALLBACK(channel_destroy), manager);
    list = spice_session_get_channels(priv->session);
    for (it = g_list_first(list); it != NULL; it = g_list_next(it)) {
        channel_new(priv->session, it->data, (gpointer*)manager);
    }
    g_list_free(list);

    return TRUE;
#endif
}

static void spice_usb_device_manager_dispose(GObject *gobject)
{
#ifdef USE_USBREDIR
    SpiceUsbDeviceManager *manager = SPICE_USB_DEVICE_MANAGER(gobject);
    SpiceUsbDeviceManagerPrivate *priv = manager->priv;

    spice_usb_backend_deregister_hotplug(priv->context);

#endif

    /* Chain up to the parent class */
    if (G_OBJECT_CLASS(spice_usb_device_manager_parent_class)->dispose) {
        G_OBJECT_CLASS(spice_usb_device_manager_parent_class)->dispose(gobject);
    }
}

static void spice_usb_device_manager_finalize(GObject *gobject)
{
    SpiceUsbDeviceManager *manager = SPICE_USB_DEVICE_MANAGER(gobject);
    SpiceUsbDeviceManagerPrivate *priv = manager->priv;

#ifdef USE_USBREDIR
    g_ptr_array_unref(priv->channels);
    if (priv->devices) {
        g_ptr_array_unref(priv->devices);
    }
    if (priv->context) {
        spice_usb_backend_delete(priv->context);
    }
    free(priv->auto_conn_filter_rules);
    free(priv->redirect_on_connect_rules);
    free(priv->policy_filter_rules);
#ifdef G_OS_WIN32
    _usbdk_hider_clear(manager);
    usbdk_api_unload(priv->usbdk_api);
#endif
#endif

    g_free(priv->auto_connect_filter);
    g_free(priv->redirect_on_connect);
    g_free(priv->policy_filter);

    /* Chain up to the parent class */
    if (G_OBJECT_CLASS(spice_usb_device_manager_parent_class)->finalize) {
        G_OBJECT_CLASS(spice_usb_device_manager_parent_class)->finalize(gobject);
    }
}

static void spice_usb_device_manager_initable_iface_init(GInitableIface *iface)
{
    iface->init = spice_usb_device_manager_initable_init;
}

static void spice_usb_device_manager_get_property(GObject     *gobject,
                                                  guint        prop_id,
                                                  GValue      *value,
                                                  GParamSpec  *pspec)
{
    SpiceUsbDeviceManager *manager = SPICE_USB_DEVICE_MANAGER(gobject);
    SpiceUsbDeviceManagerPrivate *priv = manager->priv;

    switch (prop_id) {
    case PROP_SESSION:
        g_value_set_object(value, priv->session);
        break;
    case PROP_AUTO_CONNECT:
        g_value_set_boolean(value, priv->auto_connect);
        break;
    case PROP_AUTO_CONNECT_FILTER:
        g_value_set_string(value, priv->auto_connect_filter);
        break;
    case PROP_REDIRECT_ON_CONNECT:
        g_value_set_string(value, priv->redirect_on_connect);
        break;
    case PROP_POLICY_FILTER:
        g_value_set_string(value, priv->policy_filter);
        break;
    case PROP_FREE_CHANNELS: {
        int free_channels = 0;
#ifdef USE_USBREDIR
        int i;
        for (i = 0; i < priv->channels->len; i++) {
            SpiceUsbredirChannel *channel = g_ptr_array_index(priv->channels, i);

            if (!spice_usbredir_channel_get_device(channel)) {
                free_channels++;
            }
        }
#endif
        g_value_set_int(value, free_channels);
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, prop_id, pspec);
        break;
    }
}

static void spice_usb_device_manager_set_property(GObject       *gobject,
                                                  guint          prop_id,
                                                  const GValue  *value,
                                                  GParamSpec    *pspec)
{
    SpiceUsbDeviceManager *manager = SPICE_USB_DEVICE_MANAGER(gobject);
    SpiceUsbDeviceManagerPrivate *priv = manager->priv;

    switch (prop_id) {
    case PROP_SESSION:
        priv->session = g_value_get_object(value);
        break;
    case PROP_AUTO_CONNECT:
        priv->auto_connect = g_value_get_boolean(value);
#if defined(G_OS_WIN32) && defined(USE_USBREDIR)
        _usbdk_hider_update(manager);
#endif
        break;
    case PROP_AUTO_CONNECT_FILTER: {
        const gchar *filter = g_value_get_string(value);
#ifdef USE_USBREDIR
        struct usbredirfilter_rule *rules;
        int r, count;

        r = usbredirfilter_string_to_rules(filter, ",", "|", &rules, &count);
        if (r) {
            if (r == -ENOMEM) {
                g_error("Failed to allocate memory for auto-connect-filter");
            }
            g_warning("Error parsing auto-connect-filter string, keeping old filter");
            break;
        }

        SPICE_DEBUG("auto-connect filter set to %s", filter);
        free(priv->auto_conn_filter_rules);
        priv->auto_conn_filter_rules = rules;
        priv->auto_conn_filter_rules_count = count;
#endif
        g_free(priv->auto_connect_filter);
        priv->auto_connect_filter = g_strdup(filter);

#if defined(G_OS_WIN32) && defined(USE_USBREDIR)
        _usbdk_hider_update(manager);
#endif
        break;
    }
    case PROP_REDIRECT_ON_CONNECT: {
        const gchar *filter = g_value_get_string(value);
#ifdef USE_USBREDIR
        struct usbredirfilter_rule *rules = NULL;
        int r = 0, count = 0;

        if (filter) {
            r = usbredirfilter_string_to_rules(filter, ",", "|",
                                               &rules, &count);
        }
        if (r) {
            if (r == -ENOMEM) {
                g_error("Failed to allocate memory for redirect-on-connect");
            }
            g_warning("Error parsing redirect-on-connect string, keeping old filter");
            break;
        }

        SPICE_DEBUG("redirect-on-connect filter set to %s", filter ? filter : "(null)");
        free(priv->redirect_on_connect_rules);
        priv->redirect_on_connect_rules = rules;
        priv->redirect_on_connect_rules_count = count;
#endif
        g_free(priv->redirect_on_connect);
        priv->redirect_on_connect = g_strdup(filter);
        break;
    }
    case PROP_POLICY_FILTER: {
        const gchar *filter = g_value_get_string(value);
#ifdef USE_USBREDIR
        struct usbredirfilter_rule *rules = NULL;
        int r = 0, count = 0;

        if (filter) {
            r = usbredirfilter_string_to_rules(filter, ",", "|",
                                               &rules, &count);
        }
        if (r) {
            if (r == -ENOMEM) {
                g_error("Failed to allocate memory for policy-filter");
            }
            g_warning("Error parsing policy-filter string, keeping old filter");
            break;
        }

        SPICE_DEBUG("policy filter set to %s", filter ? filter : "(null)");
        free(priv->policy_filter_rules);
        priv->policy_filter_rules = rules;
        priv->policy_filter_rules_count = count;
#endif
        g_free(priv->policy_filter);
        priv->policy_filter = g_strdup(filter);
        break;
    }
default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, prop_id, pspec);
        break;
    }
}

static void spice_usb_device_manager_class_init(SpiceUsbDeviceManagerClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
    GParamSpec *pspec;

    gobject_class->dispose      = spice_usb_device_manager_dispose;
    gobject_class->finalize     = spice_usb_device_manager_finalize;
    gobject_class->get_property = spice_usb_device_manager_get_property;
    gobject_class->set_property = spice_usb_device_manager_set_property;

    /**
     * SpiceUsbDeviceManager:session:
     *
     * #SpiceSession this #SpiceUsbDeviceManager is associated with
     *
     **/
    g_object_class_install_property
        (gobject_class, PROP_SESSION,
         g_param_spec_object("session",
                             "Session",
                             "SpiceSession",
                             SPICE_TYPE_SESSION,
                             G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS));

    /**
     * SpiceUsbDeviceManager:auto-connect:
     *
     * Set this to TRUE to automatically redirect newly plugged in device.
     *
     * Note when #SpiceGtkSession's auto-usbredir property is TRUE, this
     * property is controlled by #SpiceGtkSession.
     */
    pspec = g_param_spec_boolean("auto-connect", "Auto Connect",
                                 "Auto connect plugged in USB devices",
                                 FALSE,
                                 G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property(gobject_class, PROP_AUTO_CONNECT, pspec);

    /**
     * SpiceUsbDeviceManager:auto-connect-filter:
     *
     * Set a string specifying a filter to use to determine which USB devices
     * to autoconnect when plugged in, a filter consists of one or more rules.
     * Where each rule has the form of:
     *
     * @class,@vendor,@product,@version,@allow
     *
     * Use -1 for @class/@vendor/@product/@version to accept any value.
     *
     * And the rules themselves are concatenated like this:
     *
     * @rule1|@rule2|@rule3
     *
     * The default setting filters out HID (class 0x03) USB devices from auto
     * connect and auto connects anything else. Note the explicit allow rule at
     * the end, this is necessary since by default all devices without a
     * matching filter rule will not auto-connect.
     *
     * Filter strings in this format can be easily created with the RHEV-M
     * USB filter editor tool.
     */
    pspec = g_param_spec_string("auto-connect-filter", "Auto Connect Filter ",
               "Filter determining which USB devices to auto connect",
               "0x03,-1,-1,-1,0|-1,-1,-1,-1,1",
               G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property(gobject_class, PROP_AUTO_CONNECT_FILTER,
                                    pspec);

    /**
     * SpiceUsbDeviceManager:redirect-on-connect:
     *
     * Set a string specifying a filter selecting USB devices to automatically
     * redirect after a Spice connection has been established.
     *
     * See #SpiceUsbDeviceManager:auto-connect-filter for the filter string
     * format.
     */
    pspec = g_param_spec_string("redirect-on-connect", "Redirect on connect",
               "Filter selecting USB devices to redirect on connect", NULL,
               G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property(gobject_class, PROP_REDIRECT_ON_CONNECT,
                                    pspec);


    /**
     * SpiceUsbDeviceManager:policy-filter:
     *
     * Set a string specifying a filter to determine which USB devices are allowed
     * to be redirected by the client (affects manual redirection, and can be
     * reused for auto-redirection policies).
     *
     * See #SpiceUsbDeviceManager:auto-connect-filter for the filter-string format.
     */
    pspec = g_param_spec_string("policy-filter", "Policy filter",
                               "Client-side USB redirection policy filter",
                               NULL,
                               G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property(gobject_class, PROP_POLICY_FILTER, pspec);

    /**
     * SpiceUsbDeviceManager:free-channels:
     *
     * Get the number of available channels for redirecting USB devices.
     *
     * Since: 0.31
     */
    pspec = g_param_spec_int("free-channels", "Free channels",
                             "The number of available channels for redirecting USB devices",
                             0,
                             G_MAXINT,
                             0,
                             G_PARAM_READABLE);
    g_object_class_install_property(gobject_class, PROP_FREE_CHANNELS, pspec);

    /**
     * SpiceUsbDeviceManager::device-added:
     * @manager: the #SpiceUsbDeviceManager that emitted the signal
     * @device: #SpiceUsbDevice boxed object corresponding to the added device
     *
     * The #SpiceUsbDeviceManager::device-added signal is emitted whenever
     * a new USB device has been plugged in.
     **/
    signals[DEVICE_ADDED] =
        g_signal_new("device-added",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(SpiceUsbDeviceManagerClass, device_added),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__BOXED,
                     G_TYPE_NONE,
                     1,
                     SPICE_TYPE_USB_DEVICE);

    /**
     * SpiceUsbDeviceManager::device-removed:
     * @manager: the #SpiceUsbDeviceManager that emitted the signal
     * @device: #SpiceUsbDevice boxed object corresponding to the removed device
     *
     * The #SpiceUsbDeviceManager::device-removed signal is emitted whenever
     * an USB device has been removed.
     **/
    signals[DEVICE_REMOVED] =
        g_signal_new("device-removed",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(SpiceUsbDeviceManagerClass, device_removed),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__BOXED,
                     G_TYPE_NONE,
                     1,
                     SPICE_TYPE_USB_DEVICE);

    /**
     * SpiceUsbDeviceManager::auto-connect-failed:
     * @manager: the #SpiceUsbDeviceManager that emitted the signal
     * @device: #SpiceUsbDevice boxed object corresponding to the device which failed to auto connect
     * @error: #GError describing the reason why the autoconnect failed
     *
     * The #SpiceUsbDeviceManager::auto-connect-failed signal is emitted
     * whenever the auto-connect property is true, and a newly plugged in
     * device could not be auto-connected.
     **/
    signals[AUTO_CONNECT_FAILED] =
        g_signal_new("auto-connect-failed",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(SpiceUsbDeviceManagerClass, auto_connect_failed),
                     NULL, NULL,
                     g_cclosure_user_marshal_VOID__BOXED_BOXED,
                     G_TYPE_NONE,
                     2,
                     SPICE_TYPE_USB_DEVICE,
                     G_TYPE_ERROR);

    /**
     * SpiceUsbDeviceManager::device-error:
     * @manager: #SpiceUsbDeviceManager that emitted the signal
     * @device:  #SpiceUsbDevice boxed object corresponding to the device which has an error
     * @error:   #GError describing the error
     *
     * The #SpiceUsbDeviceManager::device-error signal is emitted whenever an
     * error happens which causes a device to no longer be available to the
     * guest.
     **/
    signals[DEVICE_ERROR] =
        g_signal_new("device-error",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(SpiceUsbDeviceManagerClass, device_error),
                     NULL, NULL,
                     g_cclosure_user_marshal_VOID__BOXED_BOXED,
                     G_TYPE_NONE,
                     2,
                     SPICE_TYPE_USB_DEVICE,
                     G_TYPE_ERROR);
}

/**
 * spice_usb_device_get_libusb_device:
 * @device: #SpiceUsbDevice to get the descriptor information of
 *
 * Finds the %libusb_device associated with the @device.
 *
 * Returns: (transfer none): the %libusb_device associated to %SpiceUsbDevice.
 *
 * Since: 0.27
 **/
gconstpointer
spice_usb_device_get_libusb_device(const SpiceUsbDevice *info G_GNUC_UNUSED)
{
#ifdef USE_USBREDIR
    g_return_val_if_fail(info != NULL, FALSE);

    return spice_usb_backend_device_get_libdev(info);
#endif
    return NULL;
}

#ifdef USE_USBREDIR

#ifdef USE_USBREDIR
/* ------------------------------------------------------------------ */
/* Smartcard/CCID detection & policy evaluation                        */

/*
 * Policy string extensions (client-side):
 *
 * In addition to raw usbredir filter syntax ("A,B,C,D,E|..."), we accept
 * human-friendly policy strings:
 *
 *   allow:<token>[,<token>...]
 *   block:<token>[,<token>...]
 *
 * Tokens are semantic USB groups. Currently supported:
 *   - smartcard  : CCID readers (interface class 0x0B)
 *   - storage    : USB Mass Storage (device class 0x08)
 *   - printer    : USB Printer (device class 0x07)
 *   - audio      : USB Audio (device class 0x01)
 *   - video      : USB Video (device class 0x0E)
 *   - hid        : Human Interface Device (device class 0x03)
 *
 * Semantics:
 *   - allow:...  -> only listed tokens are allowed, everything else denied
 *   - block:...  -> listed tokens are denied, everything else allowed
 */

/* Forward declarations */
static gboolean usb_device_has_interface_class(SpiceUsbDevice *device, guint8 klass);

static gboolean
usb_device_get_device_class(SpiceUsbDevice *device, guint8 *out_class)
{
    libusb_device *libdev;
    struct libusb_device_descriptor desc;

    if (out_class) {
        *out_class = 0;
    }

    g_return_val_if_fail(device != NULL, FALSE);

    libdev = (libusb_device *)spice_usb_device_get_libusb_device(device);
    if (!libdev) {
        return FALSE;
    }

    if (libusb_get_device_descriptor(libdev, &desc) != 0) {
        return FALSE;
    }

    if (out_class) {
        *out_class = desc.bDeviceClass;
    }
    return TRUE;
}

static gboolean
usb_device_matches_token(SpiceUsbDevice *device, const gchar *token)
{
    guint8 klass = 0;

    g_return_val_if_fail(device != NULL, FALSE);
    g_return_val_if_fail(token != NULL, FALSE);

    if (g_ascii_strcasecmp(token, "smartcard") == 0) {
        /* CCID is usually exposed as interface class 0x0B */
        return usb_device_has_interface_class(device, 0x0B);
    }

    if (!usb_device_get_device_class(device, &klass)) {
        return FALSE;
    }

    if (g_ascii_strcasecmp(token, "storage") == 0) {
        return klass == 0x08;
    }
    if (g_ascii_strcasecmp(token, "printer") == 0) {
        return klass == 0x07;
    }
    if (g_ascii_strcasecmp(token, "audio") == 0) {
        return klass == 0x01;
    }
    if (g_ascii_strcasecmp(token, "video") == 0) {
        return klass == 0x0E;
    }
    if (g_ascii_strcasecmp(token, "hid") == 0) {
        return klass == 0x03;
    }

    return FALSE;
}

/* Evaluate allow:/block: policy strings. Returns TRUE on successful parse. */
static gboolean
eval_human_policy(const gchar *spec, SpiceUsbDevice *device, gboolean *out_allow)
{
    gboolean is_allow_list = FALSE;
    const gchar *list = NULL;
    gchar **tokens = NULL;
    gboolean matched = FALSE;
    int i;

    if (!spec || !*spec || !device || !out_allow) {
        return FALSE;
    }

    if (g_str_has_prefix(spec, "allow:")) {
        is_allow_list = TRUE;
        list = spec + 6;
    } else if (g_str_has_prefix(spec, "block:")) {
        is_allow_list = FALSE;
        list = spec + 6;
    } else {
        return FALSE;
    }

    if (!list || !*list) {
        return FALSE;
    }

    tokens = g_strsplit(list, ",", -1);
    for (i = 0; tokens && tokens[i]; i++) {
        gchar *t = g_strstrip(tokens[i]);
        if (!t || !*t) {
            continue;
        }
        if (usb_device_matches_token(device, t)) {
            matched = TRUE;
            break;
        }
    }
    g_strfreev(tokens);

    if (is_allow_list) {
        *out_allow = matched;
    } else {
        *out_allow = !matched;
    }

    return TRUE;
}

/* Return TRUE if the USB device exposes an interface with the given class */
static gboolean
usb_device_has_interface_class(SpiceUsbDevice *device, guint8 klass)
{
    libusb_device *libdev;
    struct libusb_config_descriptor *config = NULL;
    int rc;
    int i, j;

    g_return_val_if_fail(device != NULL, FALSE);

    libdev = (libusb_device *)spice_usb_device_get_libusb_device(device);
    if (!libdev) {
        return FALSE;
    }

    /* Try active config first; fall back to config 0 for unconfigured devices */
    rc = libusb_get_active_config_descriptor(libdev, &config);
    if (rc != 0) {
        rc = libusb_get_config_descriptor(libdev, 0, &config);
    }
    if (rc != 0 || !config) {
        return FALSE;
    }

    for (i = 0; i < (int)config->bNumInterfaces; i++) {
        const struct libusb_interface *itf = &config->interface[i];
        for (j = 0; j < itf->num_altsetting; j++) {
            const struct libusb_interface_descriptor *alt = &itf->altsetting[j];
            if (alt->bInterfaceClass == klass) {
                libusb_free_config_descriptor(config);
                return TRUE;
            }
        }
    }

    libusb_free_config_descriptor(config);
    return FALSE;
}

static gboolean
usb_device_get_bcd_device(SpiceUsbDevice *device, guint16 *out_bcd)
{
    libusb_device *libdev;
    struct libusb_device_descriptor desc;

    if (out_bcd) {
        *out_bcd = 0;
    }

    libdev = (libusb_device *)spice_usb_device_get_libusb_device(device);
    if (!libdev) {
        return FALSE;
    }

    if (libusb_get_device_descriptor(libdev, &desc) != 0) {
        return FALSE;
    }

    if (out_bcd) {
        *out_bcd = desc.bcdDevice;
    }
    return TRUE;
}

static gboolean
parse_int16_token(const gchar *s, gint *out)
{
    gchar *end = NULL;
    long long v;

    if (!s || !*s) {
        return FALSE;
    }

    if (g_strcmp0(s, "-1") == 0) {
        *out = -1;
        return TRUE;
    }

    errno = 0;
    v = g_ascii_strtoll(s, &end, 0);
    if (errno != 0 || end == s || (end && *end != '\0')) {
        return FALSE;
    }

    if (v < 0 || v > 0xFFFF) {
        return FALSE;
    }

    *out = (gint)v;
    return TRUE;
}

/*
 * Evaluate a usbredir filter string (A,B,C,D,E|...) against a synthetic class
 * and the actual device VID/PID/bcdDevice.
 *
 * Returns TRUE if a matching rule was found and sets *out_allow accordingly.
 * Returns FALSE if no rule matched or parse failed.
 *
 * Note: usbredirfilter semantics are "first match wins" in typical deployments
 * (allow specific rules followed by a catch-all).
 */
static gboolean
eval_filter_string_first_match(const gchar *filter_in,
                               gint match_class,
                               gint vid, gint pid, gint bcd,
                               gboolean *out_allow)
{
    gchar *filter = NULL;
    gchar **rules = NULL;
    int ri;

    if (!filter_in || !*filter_in) {
        return FALSE;
    }

    /* Accept optional "raw:" prefix */
    if (g_str_has_prefix(filter_in, "raw:")) {
        filter = g_strdup(filter_in + 4);
    } else {
        filter = g_strdup(filter_in);
    }

    rules = g_strsplit(filter, "|", -1);
    for (ri = 0; rules && rules[ri]; ri++) {
        gchar **fields = NULL;
        gint a = -1, b = -1, c = -1, d = -1, e = -1;

        if (rules[ri][0] == '\0') {
            continue;
        }

        fields = g_strsplit(rules[ri], ",", 5);
        /* Need exactly 5 fields */
        if (!fields || !fields[0] || !fields[1] || !fields[2] || !fields[3] || !fields[4]) {
            g_strfreev(fields);
            continue;
        }

        if (!parse_int16_token(fields[0], &a) ||
            !parse_int16_token(fields[1], &b) ||
            !parse_int16_token(fields[2], &c) ||
            !parse_int16_token(fields[3], &d) ||
            !parse_int16_token(fields[4], &e)) {
            g_strfreev(fields);
            g_strfreev(rules);
            g_free(filter);
            return FALSE;
        }
        g_strfreev(fields);

        /* Match */
        if (a != -1 && a != match_class) {
            continue;
        }
        if (b != -1 && b != vid) {
            continue;
        }
        if (c != -1 && c != pid) {
            continue;
        }
        if (d != -1 && d != bcd) {
            continue;
        }

        if (out_allow) {
            *out_allow = (e != 0);
        }
        g_strfreev(rules);
        g_free(filter);
        return TRUE;
    }

    g_strfreev(rules);
    g_free(filter);
    return FALSE;
}
#endif /* USE_USBREDIR */

/* ------------------------------------------------------------------ */
/* callbacks                                                          */

static void channel_new(SpiceSession *session, SpiceChannel *channel,
                        gpointer user_data)
{
    SpiceUsbDeviceManager *manager = user_data;

    if (!SPICE_IS_USBREDIR_CHANNEL(channel)) {
        return;
    }

    spice_usbredir_channel_set_context(SPICE_USBREDIR_CHANNEL(channel),
                                       manager->priv->context);
    spice_channel_connect(channel);
    g_ptr_array_add(manager->priv->channels, channel);

    g_signal_connect(channel, "channel-event", G_CALLBACK(channel_event), manager);

    spice_usb_device_manager_check_redir_on_connect(manager, channel);

    /*
     * add a reference to ourself, to make sure the libusb context is
     * alive as long as the channel is.
     * TODO: moving to gusb could help here too.
     */
    g_object_ref(manager);
    g_object_weak_ref(G_OBJECT(channel), (GWeakNotify)g_object_unref, manager);
}

static void channel_destroy(SpiceSession *session, SpiceChannel *channel,
                            gpointer user_data)
{
    SpiceUsbDeviceManager *manager = user_data;

    if (!SPICE_IS_USBREDIR_CHANNEL(channel)) {
        return;
    }

    g_ptr_array_remove(manager->priv->channels, channel);
}

static void channel_event(SpiceChannel *channel, SpiceChannelEvent event,
                          gpointer user_data)
{
    SpiceUsbDeviceManager *manager = user_data;

    switch (event) {
    case SPICE_CHANNEL_NONE:
    case SPICE_CHANNEL_OPENED:
        return;

    case SPICE_CHANNEL_SWITCHING:
    case SPICE_CHANNEL_CLOSED:
    case SPICE_CHANNEL_ERROR_CONNECT:
    case SPICE_CHANNEL_ERROR_TLS:
    case SPICE_CHANNEL_ERROR_LINK:
    case SPICE_CHANNEL_ERROR_AUTH:
    case SPICE_CHANNEL_ERROR_IO:
        g_signal_handlers_disconnect_by_func(channel, channel_event, user_data);
        g_ptr_array_remove(manager->priv->channels, channel);
        return;
    default:
        g_warning("Unhandled SpiceChannelEvent %u, disconnecting usbredir %p", event, channel);
        g_signal_handlers_disconnect_by_func(channel, channel_event, user_data);
        g_ptr_array_remove(manager->priv->channels, channel);
    }
}

static void spice_usb_device_manager_auto_connect_cb(GObject      *gobject,
                                                     GAsyncResult *res,
                                                     gpointer      user_data)
{
    SpiceUsbDeviceManager *manager = SPICE_USB_DEVICE_MANAGER(gobject);
    SpiceUsbDevice *device = user_data;
    GError *err = NULL;

    spice_usb_device_manager_connect_device_finish(manager, res, &err);
    if (err) {
        gchar *desc = spice_usb_device_get_description(device, NULL);
        g_prefix_error(&err, "Could not auto-redirect %s: ", desc);
        g_free(desc);

        SPICE_DEBUG("%s", err->message);
        g_signal_emit(manager, signals[AUTO_CONNECT_FAILED], 0, device, err);
        g_error_free(err);
    }
    spice_usb_device_unref(device);
}

static gboolean
spice_usb_device_match(SpiceUsbDevice *device,
                       const int bus, const int address)
{
    return (spice_usb_device_get_busnum(device) == bus &&
            spice_usb_device_get_devaddr(device) == address);
}

static SpiceUsbDevice*
spice_usb_device_manager_find_device(SpiceUsbDeviceManager *manager,
                                     const int bus, const int address)
{
    SpiceUsbDeviceManagerPrivate *priv = manager->priv;
    SpiceUsbDevice *curr, *device = NULL;
    guint i;

    for (i = 0; i < priv->devices->len; i++) {
        curr = g_ptr_array_index(priv->devices, i);
        if (spice_usb_device_match(curr, bus, address)) {
            device = curr;
            break;
        }
    }
    return device;
}

static void spice_usb_device_manager_add_dev(SpiceUsbDeviceManager *manager,
                                             SpiceUsbDevice *bdev)
{
    SpiceUsbDeviceManagerPrivate *priv = manager->priv;
    const UsbDeviceInformation *b_info = spice_usb_backend_device_get_info(bdev);
    SpiceUsbDevice *device;

    if (spice_usb_device_manager_find_device(manager,
                                             b_info->bus,
                                             b_info->address)) {
        SPICE_DEBUG("device not added %d:%d %04x:%04x",
                    b_info->bus,
                    b_info->address,
                    b_info->vid,
                    b_info->pid);
        return;
    }

    device = spice_usb_device_new(bdev);
    if (!device) {
        return;
    }

    g_ptr_array_add(priv->devices, device);

    if (priv->auto_connect) {
        gboolean can_redirect, auto_ok;

        can_redirect = spice_usb_device_manager_can_redirect_device(manager, device, NULL);

        auto_ok = spice_usb_backend_device_check_filter(bdev,
                                                        priv->auto_conn_filter_rules,
                                                        priv->auto_conn_filter_rules_count) == 0;

        if (can_redirect && auto_ok) {
            spice_usb_device_manager_connect_device_async(manager, device, NULL,
                                                          spice_usb_device_manager_auto_connect_cb,
                                                          spice_usb_device_ref(device));
        }
    }

    SPICE_DEBUG("device added %04x:%04x (%p)",
                spice_usb_device_get_vid(device),
                spice_usb_device_get_pid(device),
                device);
    g_signal_emit(manager, signals[DEVICE_ADDED], 0, device);
}

static void spice_usb_device_manager_remove_dev(SpiceUsbDeviceManager *manager,
                                                SpiceUsbDevice *bdev)
{
    SpiceUsbDeviceManagerPrivate *priv = manager->priv;
    SpiceUsbDevice *device;
    const UsbDeviceInformation *b_info = spice_usb_backend_device_get_info(bdev);

    device = spice_usb_device_manager_find_device(manager, b_info->bus, b_info->address);
    if (!device) {
        g_warning("Could not find USB device to remove " DEV_ID_FMT,
                  b_info->bus, b_info->address);
        return;
    }

    /* TODO: check usage of the sync version is ok here */
    disconnect_device_sync(manager, device);

    SPICE_DEBUG("device removed %04x:%04x (%p)",
                spice_usb_device_get_vid(device),
                spice_usb_device_get_pid(device),
                device);
    spice_usb_device_ref(device);
    g_ptr_array_remove(priv->devices, device);
    g_signal_emit(manager, signals[DEVICE_REMOVED], 0, device);
    spice_usb_device_unref(device);
}

struct hotplug_idle_cb_args {
    SpiceUsbDeviceManager *manager;
    SpiceUsbDevice *device;
    gboolean               added;
};

static gboolean spice_usb_device_manager_hotplug_idle_cb(gpointer user_data)
{
    struct hotplug_idle_cb_args *args = user_data;
    SpiceUsbDeviceManager *manager = SPICE_USB_DEVICE_MANAGER(args->manager);

    if (args->added) {
        spice_usb_device_manager_add_dev(manager, args->device);
    } else {
        spice_usb_device_manager_remove_dev(manager, args->device);
    }

    spice_usb_backend_device_unref(args->device);
    g_object_unref(manager);
    g_free(args);
    return FALSE;
}

/* Can be called from both the main-thread as well as the event_thread */
static void spice_usb_device_manager_hotplug_cb(void *user_data,
                                                SpiceUsbDevice *dev,
                                                gboolean added)
{
    SpiceUsbDeviceManager *manager = SPICE_USB_DEVICE_MANAGER(user_data);
    struct hotplug_idle_cb_args *args = g_malloc0(sizeof(*args));

    args->manager = g_object_ref(manager);
    args->device = spice_usb_backend_device_ref(dev);
    args->added = added;
    g_idle_add(spice_usb_device_manager_hotplug_idle_cb, args);
}

static void spice_usb_device_manager_channel_connect_cb(GObject *gobject,
                                                        GAsyncResult *channel_res,
                                                        gpointer user_data)
{
    SpiceUsbredirChannel *channel = SPICE_USBREDIR_CHANNEL(gobject);
    GTask *task = G_TASK(user_data);
    GError *err = NULL;

    spice_usbredir_channel_connect_device_finish(channel, channel_res, &err);
    if (err) {
        g_task_return_error(task, err);
    } else {
        g_task_return_boolean(task, TRUE);
    }

    g_object_unref(task);
}

/* ------------------------------------------------------------------ */
/* private api                                                        */

static void spice_usb_device_manager_check_redir_on_connect(SpiceUsbDeviceManager *manager,
                                                            SpiceChannel *channel)
{
    SpiceUsbDeviceManagerPrivate *priv = manager->priv;
    GTask *task;
    SpiceUsbDevice *device;
    gboolean is_cd, shall_redirect;
    guint i;

    if (priv->redirect_on_connect == NULL) {
        return;
    }

    for (i = 0; i < priv->devices->len; i++) {
        device = g_ptr_array_index(priv->devices, i);

        if (spice_usb_device_manager_is_device_connected(manager, device)) {
            continue;
        }

        is_cd = spice_usb_device_manager_is_device_shared_cd(manager, device);

        if (is_cd) {
            shall_redirect = TRUE;
        } else if (priv->redirect_on_connect) {
            const struct usbredirfilter_rule *rules = priv->redirect_on_connect_rules;
            int rules_count = priv->redirect_on_connect_rules_count;

            shall_redirect = !spice_usb_backend_device_check_filter(device, rules, rules_count);
        } else {
            shall_redirect = FALSE;
        }

        if (shall_redirect) {
            /* Note: re-uses spice_usb_device_manager_connect_device_async's
               completion handling code! */
            task = g_task_new(manager,
                              NULL,
                              spice_usb_device_manager_auto_connect_cb,
                              spice_usb_device_ref(device));

            spice_usbredir_channel_connect_device_async(SPICE_USBREDIR_CHANNEL(channel),
                                                        device, NULL,
                                                        spice_usb_device_manager_channel_connect_cb,
                                                        task);
            return; /* We've taken the channel! */
        }
    }
}

static void
spice_usb_device_manager_handle_disconnect(SpiceUsbDeviceManager *manager,
                                           SpiceUsbDevice *device)
{
    if (spice_usb_device_manager_is_device_shared_cd(manager, device)) {
        spice_usb_backend_device_eject(manager->priv->context, device);
    }
}

void spice_usb_device_manager_device_error(SpiceUsbDeviceManager *manager,
                                           SpiceUsbDevice *device,
                                           GError *err)
{
    g_return_if_fail(SPICE_IS_USB_DEVICE_MANAGER(manager));
    g_return_if_fail(device != NULL);

    g_signal_emit(manager, signals[DEVICE_ERROR], 0, device, err);
    spice_usb_device_manager_handle_disconnect(manager, device);
}
#endif

static SpiceUsbredirChannel *
spice_usb_device_manager_get_channel_for_dev(SpiceUsbDeviceManager *manager,
                                             SpiceUsbDevice *device)
{
#ifdef USE_USBREDIR
    SpiceUsbDeviceManagerPrivate *priv = manager->priv;
    guint i;

    for (i = 0; i < priv->channels->len; i++) {
        SpiceUsbredirChannel *channel = g_ptr_array_index(priv->channels, i);
        spice_usbredir_channel_lock(channel);
        SpiceUsbDevice *bdev = spice_usbredir_channel_get_device(channel);
        if (spice_usb_manager_device_equal_bdev(manager, device, bdev)) {
            spice_usbredir_channel_unlock(channel);
            return channel;
        }
        spice_usbredir_channel_unlock(channel);
    }
#endif
    return NULL;
}

/* ------------------------------------------------------------------ */
/* public api                                                         */

/**
 * spice_usb_device_manager_get_devices_with_filter:
 * @manager: the #SpiceUsbDeviceManager manager
 * @filter: (allow-none): filter string for selecting which devices to return,
 *      see #SpiceUsbDeviceManager:auto-connect-filter for the filter
 *      string format
 *
 * Finds devices associated with the @manager complying with the @filter
 *
 * Returns: (element-type SpiceUsbDevice) (transfer full): a
 * %GPtrArray array of %SpiceUsbDevice
 *
 * Since: 0.20
 */
GPtrArray* spice_usb_device_manager_get_devices_with_filter(SpiceUsbDeviceManager *manager,
                                                            const gchar *filter)
{
    GPtrArray *devices_copy = NULL;

    g_return_val_if_fail(SPICE_IS_USB_DEVICE_MANAGER(manager), NULL);

#ifdef USE_USBREDIR
    SpiceUsbDeviceManagerPrivate *priv = manager->priv;
    struct usbredirfilter_rule *rules = NULL;
    int r, count = 0;
    guint i;

    if (filter) {
        r = usbredirfilter_string_to_rules(filter, ",", "|", &rules, &count);
        if (r) {
            if (r == -ENOMEM) {
                g_error("Failed to allocate memory for filter");
            }
            g_warning("Error parsing filter, ignoring");
            rules = NULL;
            count = 0;
        }
    }

    devices_copy = g_ptr_array_new_with_free_func((GDestroyNotify)
                                                  spice_usb_device_unref);
    for (i = 0; i < priv->devices->len; i++) {
        SpiceUsbDevice *device = g_ptr_array_index(priv->devices, i);

        if (rules) {
            gboolean filter_ok =
                (spice_usb_backend_device_check_filter(device, rules, count) == 0);
            if (!filter_ok) {
                continue;
            }
        }
        g_ptr_array_add(devices_copy, spice_usb_device_ref(device));
    }

    free(rules);
#endif

    return devices_copy;
}

/**
 * spice_usb_device_manager_get_devices:
 * @manager: the #SpiceUsbDeviceManager manager
 *
 * Finds devices associated with the @manager
 *
 * Returns: (element-type SpiceUsbDevice) (transfer full): a %GPtrArray array of %SpiceUsbDevice
 */
GPtrArray* spice_usb_device_manager_get_devices(SpiceUsbDeviceManager *manager)
{
    return spice_usb_device_manager_get_devices_with_filter(manager, NULL);
}

/**
 * spice_usb_device_manager_is_device_connected:
 * @manager: the #SpiceUsbDeviceManager manager
 * @device: a #SpiceUsbDevice
 *
 * Finds if the @device is connected.
 *
 * Returns: %TRUE if @device has an associated USB redirection channel
 */
gboolean spice_usb_device_manager_is_device_connected(SpiceUsbDeviceManager *manager,
                                                      SpiceUsbDevice *device)
{
    g_return_val_if_fail(SPICE_IS_USB_DEVICE_MANAGER(manager), FALSE);
    g_return_val_if_fail(device != NULL, FALSE);

    return !!spice_usb_device_manager_get_channel_for_dev(manager, device);
}

#ifdef USE_USBREDIR

static gboolean
_spice_usb_device_manager_connect_device_finish(SpiceUsbDeviceManager *manager,
                                                GAsyncResult *res,
                                                GError **error)
{
    GTask *task = G_TASK(res);

    g_return_val_if_fail(g_task_is_valid(task, G_OBJECT(manager)), FALSE);

    return g_task_propagate_boolean(task, error);
}

static void
_spice_usb_device_manager_connect_device_async(SpiceUsbDeviceManager *manager,
                                               SpiceUsbDevice *device,
                                               GCancellable *cancellable,
                                               GAsyncReadyCallback callback,
                                               gpointer user_data)
{
    GTask *task;

    g_return_if_fail(SPICE_IS_USB_DEVICE_MANAGER(manager));
    g_return_if_fail(device != NULL);

    SPICE_DEBUG("connecting device %p", device);

    task = g_task_new(manager, cancellable, callback, user_data);

    SpiceUsbDeviceManagerPrivate *priv = manager->priv;
    guint i;

    if (spice_usb_device_manager_is_device_connected(manager, device)) {
        g_task_return_new_error(task,
                                SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                                "Cannot connect an already connected usb device");
        goto done;
    }

    for (i = 0; i < priv->channels->len; i++) {
        SpiceUsbredirChannel *channel = g_ptr_array_index(priv->channels, i);

        if (spice_usbredir_channel_get_device(channel)) {
            continue; /* Skip already used channels */
        }

        spice_usbredir_channel_connect_device_async(channel,
                                                    device,
                                                    cancellable,
                                                    spice_usb_device_manager_channel_connect_cb,
                                                    task);
        return;
    }

    g_task_return_new_error(task,
                            SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                            _("No free USB channel"));
done:
    g_object_unref(task);
}

#endif

/**
 * spice_usb_device_manager_connect_device_async:
 * @manager: a #SpiceUsbDeviceManager.
 * @device: a #SpiceUsbDevice to redirect
 * @cancellable: (allow-none): optional #GCancellable object, %NULL to ignore
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: the data to pass to callback function
 *
 * Asynchronously connects the @device. When completed, @callback will be called.
 * Then it is possible to call spice_usb_device_manager_connect_device_finish()
 * to get the result of the operation.
 */
void spice_usb_device_manager_connect_device_async(SpiceUsbDeviceManager *manager,
                                                   SpiceUsbDevice *device,
                                                   GCancellable *cancellable,
                                                   GAsyncReadyCallback callback,
                                                   gpointer user_data)
{
    g_return_if_fail(SPICE_IS_USB_DEVICE_MANAGER(manager));

#ifdef USE_USBREDIR

    GError *local_err = NULL;
    if (!spice_usb_device_manager_can_redirect_device(manager, device, &local_err)) {
        if (callback) {
            GTask *t = g_task_new(G_OBJECT(manager), cancellable, callback, user_data);
            g_task_return_error(t, local_err);
            g_object_unref(t);
        } else {
            g_clear_error(&local_err);
        }
        return;
    }
    g_clear_error(&local_err);


    GTask *task = g_task_new(G_OBJECT(manager), cancellable, callback, user_data);

    g_task_set_task_data(task, device, NULL);

    _set_redirecting(manager, TRUE);
    _spice_usb_device_manager_connect_device_async(manager, device,
                                                   cancellable,
                                                   _connect_device_async_cb,
                                                   task);
#endif
}

/**
 * spice_usb_device_manager_connect_device_finish:
 * @manager: a #SpiceUsbDeviceManager.
 * @res: a #GAsyncResult
 * @err: (allow-none): a return location for a #GError, or %NULL.
 *
 * Finishes an async operation. See spice_usb_device_manager_connect_device_async().
 *
 * Returns: %TRUE if connection is successful
 */
gboolean spice_usb_device_manager_connect_device_finish(SpiceUsbDeviceManager *manager,
                                                        GAsyncResult *res,
                                                        GError **err)
{
    GTask *task = G_TASK(res);

    g_return_val_if_fail(g_task_is_valid(task, manager),
                         FALSE);

    return g_task_propagate_boolean(task, err);
}

/**
 * spice_usb_device_manager_disconnect_device_finish:
 * @manager: a #SpiceUsbDeviceManager.
 * @res: a #GAsyncResult
 * @err: (allow-none): a return location for a #GError, or %NULL.
 *
 * Finishes an async operation. See spice_usb_device_manager_disconnect_device_async().
 *
 * Returns: %TRUE if disconnection is successful
 */
gboolean spice_usb_device_manager_disconnect_device_finish(SpiceUsbDeviceManager *manager,
                                                           GAsyncResult *res,
                                                           GError **err)
{
    GTask *task = G_TASK(res);

    g_return_val_if_fail(g_task_is_valid(task, G_OBJECT(manager)), FALSE);

    return g_task_propagate_boolean(task, err);
}

#ifdef USE_USBREDIR
static
void _connect_device_async_cb(GObject *gobject,
                              GAsyncResult *channel_res,
                              gpointer user_data)
{
    SpiceUsbDeviceManager *manager = SPICE_USB_DEVICE_MANAGER(gobject);
    GTask *task = user_data;
    GError *err = NULL;
    gboolean rc;

    _set_redirecting(manager, FALSE);

    rc = _spice_usb_device_manager_connect_device_finish(manager, channel_res, &err);
    if (rc) {
        g_task_return_boolean(task, TRUE);
    } else {
        SpiceUsbDevice *device = g_task_get_task_data(task);

        spice_usb_device_manager_handle_disconnect(manager, device);
        g_task_return_error(task, err);
    }
    g_object_unref(task);
}
#endif

static void disconnect_device_sync(SpiceUsbDeviceManager *manager,
                                   SpiceUsbDevice *device)
{
    g_return_if_fail(SPICE_IS_USB_DEVICE_MANAGER(manager));
    g_return_if_fail(device != NULL);

    SPICE_DEBUG("disconnecting device %p", device);

#ifdef USE_USBREDIR
    SpiceUsbredirChannel *channel;

    channel = spice_usb_device_manager_get_channel_for_dev(manager, device);
    if (channel) {
        spice_usbredir_channel_disconnect_device(channel);
    }

#endif
}

/**
 * spice_usb_device_manager_disconnect_device:
 * @manager: the #SpiceUsbDeviceManager manager
 * @device: a #SpiceUsbDevice to disconnect
 *
 * Disconnects the @device.
 */
void spice_usb_device_manager_disconnect_device(SpiceUsbDeviceManager *manager,
                                                SpiceUsbDevice *device)
{
    disconnect_device_sync(manager, device);
}

#ifdef USE_USBREDIR
static
void _disconnect_device_async_cb(GObject *gobject,
                                 GAsyncResult *channel_res,
                                 gpointer user_data)
{
    SpiceUsbredirChannel *channel = SPICE_USBREDIR_CHANNEL(gobject);
    GTask *task = user_data;
    SpiceUsbDeviceManager *manager = SPICE_USB_DEVICE_MANAGER(g_task_get_source_object(task));
    SpiceUsbDevice *device = g_task_get_task_data(task);
    GError *err = NULL;

    _set_redirecting(manager, FALSE);

    spice_usbredir_channel_disconnect_device_finish(channel, channel_res, &err);
    if (err) {
        g_task_return_error(task, err);
    } else {
        g_task_return_boolean(task, TRUE);

        spice_usb_device_manager_handle_disconnect(manager, device);
    }

    g_object_unref(task);
}
#endif

/**
 * spice_usb_device_manager_disconnect_device_async:
 * @manager: the #SpiceUsbDeviceManager manager.
 * @device: a connected #SpiceUsbDevice to disconnect.
 * @cancellable: (nullable): optional #GCancellable object, %NULL to ignore.
 * @callback: (scope async): a #GAsyncReadyCallback to call when the request is satisfied.
 * @user_data: (closure): the data to pass to @callback.
 *
 * Asynchronously disconnects the @device. When completed, @callback will be called.
 * Then it is possible to call spice_usb_device_manager_disconnect_device_finish()
 * to get the result of the operation.
 *
 * Since: 0.32
 */
void spice_usb_device_manager_disconnect_device_async(SpiceUsbDeviceManager *manager,
                                                      SpiceUsbDevice *device,
                                                      GCancellable *cancellable,
                                                      GAsyncReadyCallback callback,
                                                      gpointer user_data)
{
#ifdef USE_USBREDIR
    GTask *nested;
    g_return_if_fail(SPICE_IS_USB_DEVICE_MANAGER(manager));

    g_return_if_fail(device != NULL);
    g_return_if_fail(spice_usb_device_manager_is_device_connected(manager, device));

    SPICE_DEBUG("disconnecting device %p", device);

    SpiceUsbredirChannel *channel;

    _set_redirecting(manager, TRUE);

    channel = spice_usb_device_manager_get_channel_for_dev(manager, device);
    nested  = g_task_new(G_OBJECT(manager), cancellable, callback, user_data);
    g_task_set_task_data(nested, device, NULL);

    spice_usbredir_channel_disconnect_device_async(channel, cancellable,
                                                   _disconnect_device_async_cb,
                                                   nested);
#endif
}

/**
 * spice_usb_device_manager_can_redirect_device:
 * @manager: the #SpiceUsbDeviceManager manager
 * @device: a #SpiceUsbDevice to disconnect
 * @err: (allow-none): a return location for a #GError, or %NULL.
 *
 * Checks whether it is possible to redirect the @device.
 *
 * Returns: %TRUE if @device can be redirected
 */
gboolean
spice_usb_device_manager_can_redirect_device(SpiceUsbDeviceManager *manager,
                                             SpiceUsbDevice        *device,
                                             GError                **err)
{
#ifdef USE_USBREDIR
    const struct usbredirfilter_rule *guest_filter_rules = NULL;
    SpiceUsbDeviceManagerPrivate *priv = manager->priv;
    int i, guest_filter_rules_count;

    g_return_val_if_fail(SPICE_IS_USB_DEVICE_MANAGER(manager), FALSE);
    g_return_val_if_fail(device != NULL, FALSE);
    g_return_val_if_fail(err == NULL || *err == NULL, FALSE);

    if (!spice_session_get_usbredir_enabled(priv->session)) {
        g_set_error_literal(err, SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                            _("USB redirection is disabled"));
        return FALSE;
    }

    if (!priv->channels->len) {
        g_set_error_literal(err, SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                            _("The connected VM is not configured for USB redirection"));
        return FALSE;
    }

    /* Skip the other checks for already connected devices */
    if (spice_usb_device_manager_is_device_connected(manager, device)) {
        return TRUE;
    }

    /* We assume all channels have the same filter, so we just take the
       filter from the first channel */
    spice_usbredir_channel_get_guest_filter(g_ptr_array_index(priv->channels, 0),
                                            &guest_filter_rules,
                                            &guest_filter_rules_count);

    if (guest_filter_rules) {
        gboolean filter_ok;

        filter_ok = (spice_usb_backend_device_check_filter(device,
                                                           guest_filter_rules,
                                                           guest_filter_rules_count) == 0);
        if (!filter_ok) {
            g_set_error_literal(err, SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                                _("Some USB devices are blocked by host policy"));
            return FALSE;
        }
    }


    if (priv->policy_filter_rules || priv->policy_filter) {
        gboolean policy_ok = TRUE;

        /* First, handle human-friendly allow:/block: policies */
        if (priv->policy_filter) {
            gboolean allow = TRUE;
            if (eval_human_policy(priv->policy_filter, device, &allow)) {
                policy_ok = allow;
            } else if (priv->policy_filter_rules == NULL) {
                /* No parsed rules and not a known human policy -> allow */
                policy_ok = TRUE;
            }
        }

        /* If not decided by human policy, fall back to usbredir rules */
        if (policy_ok && priv->policy_filter_rules) {

        /*
         * usbredir filter rules match on *device* class. For CCID readers,
         * the Smart Card class (0x0B) is typically exposed as an *interface*
         * class while the device class is 0x00 (composite).
         *
         * When a device exposes an interface class 0x0B, we evaluate the
         * policy-filter string against a synthetic class=0x0B so that
         * policies like "allow:smartcard" / "block:smartcard" behave as users
         * expect, without any VID/PID hardcoding.
         */
        if (priv->policy_filter && usb_device_has_interface_class(device, 0x0B)) {
            gboolean allow = FALSE;
            guint16 bcd_u16 = 0;
            gint vid = (gint)spice_usb_device_get_vid(device);
            gint pid = (gint)spice_usb_device_get_pid(device);
            gint bcd = -1;

            if (usb_device_get_bcd_device(device, &bcd_u16)) {
                bcd = (gint)bcd_u16;
            }

            if (eval_filter_string_first_match(priv->policy_filter, 0x0B, vid, pid, bcd, &allow)) {
                policy_ok = allow;
            } else {
                /* Fall back to standard device-class based evaluation */
                policy_ok = (spice_usb_backend_device_check_filter(device,
                                                                   priv->policy_filter_rules,
                                                                   priv->policy_filter_rules_count) == 0);
            }
        } else {
            policy_ok = (spice_usb_backend_device_check_filter(device,
                                                               priv->policy_filter_rules,
                                                               priv->policy_filter_rules_count) == 0);
        }

        }

        if (!policy_ok) {
            g_set_error_literal(err, SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                                _("This USB device is blocked by client policy"));
            return FALSE;
        }
    }

    /* Check if there are free channels */
    for (i = 0; i < priv->channels->len; i++) {
        SpiceUsbredirChannel *channel = g_ptr_array_index(priv->channels, i);
        spice_usbredir_channel_lock(channel);

        if (!spice_usbredir_channel_get_device(channel)){
            spice_usbredir_channel_unlock(channel);
            break;
        }
        spice_usbredir_channel_unlock(channel);
    }
    if (i == priv->channels->len) {
        g_set_error_literal(err, SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                            _("There are no free USB channels"));
        return FALSE;
    }

    return TRUE;
#else
    g_set_error_literal(err, SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                        _("USB redirection support not compiled in"));
    return FALSE;
#endif
}

/**
 * spice_usb_device_get_description:
 * @device: #SpiceUsbDevice to get the description of
 * @format: (allow-none): an optional printf() format string with
 * positional parameters
 *
 * Get a string describing the device which is suitable as a description of
 * the device for the end user. The returned string should be freed with
 * g_free() when no longer needed.
 *
 * The @format positional parameters are the following:
 * 1. \%s manufacturer
 * 2. \%s product
 * 3. \%s descriptor (a [vendor_id:product_id] string)
 * 4. \%d bus
 * 5. \%d address
 *
 * (the default format string is "\%s \%s \%s at \%d-\%d")
 *
 * Returns: a newly-allocated string holding the description, or %NULL if failed
 */
gchar *spice_usb_device_get_description(SpiceUsbDevice *device, const gchar *format)
{
#ifdef USE_USBREDIR
    g_return_val_if_fail(device != NULL, NULL);

    return spice_usb_backend_device_get_description(device, format);
#else
    return NULL;
#endif
}

/**
 * spice_usb_device_manager_create_shared_cd_device:
 * @manager: a #SpiceUsbDeviceManager
 * @filename: image or device path
 * @err: (allow-none): a return location for a #GError, or %NULL.
 *
 * Creates a new shared CD device based on a disk image file
 * or a physical CD device.
 *
 * Returns: %TRUE if device created successfully
 */
gboolean
spice_usb_device_manager_create_shared_cd_device(SpiceUsbDeviceManager *manager,
                                                 gchar *filename,
                                                 GError **err)
{
#ifdef USE_USBREDIR
    SpiceUsbDeviceManagerPrivate *priv = manager->priv;

    CdEmulationParams cd_params = {
        .filename = filename,
        .delete_on_eject = 1,
    };

    return create_emulated_cd(priv->context, &cd_params, err);
#else
    g_set_error_literal(err, SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                        _("USB redirection support not compiled in"));
    return FALSE;
#endif
}

/**
 * spice_usb_device_manager_is_device_shared_cd:
 * @manager: a #SpiceUsbDeviceManager
 * @device: a #SpiceUsbDevice to query
 *
 * Checks whether a device is shared CD.
 *
 * Returns: %TRUE if the device is shared CD
 */
gboolean
spice_usb_device_manager_is_device_shared_cd(SpiceUsbDeviceManager *manager,
                                             SpiceUsbDevice *device)
{
#ifdef USE_USBREDIR
    /* TODO This function currently return TRUE for all emulated
     * devices. Currently only shared CDs are implemented as shared
     * devices but this will break adding new emulated devices.
     * Check VID/PID in the future.
     */
    gboolean is_cd = (spice_usb_backend_device_get_libdev(device) == NULL);

    return is_cd;
#else
    return FALSE;
#endif
}

/**
 * spice_usb_device_manager_allocate_device_for_file_descriptor:
 * @manager: (type SpiceUsbDeviceManager): the #SpiceUsbDeviceManager manager.
 * @file_descriptor: an open file descriptor for the USB device.
 * @err: (allow-none): a return location for a #GError, or %NULL.
 *
 * Allocates a SpiceUsbDevice instance for the specified file descriptor.
 *
 * Returns: (nullable) (transfer full): an allocated SpiceUsbDevice instance or %NULL in case of failure.
 *
 * Since: 0.40
 */
SpiceUsbDevice *
spice_usb_device_manager_allocate_device_for_file_descriptor(SpiceUsbDeviceManager *manager,
                                                             int file_descriptor,
                                                             GError **err)
{
#ifdef USE_USBREDIR
    SpiceUsbDeviceManagerPrivate *priv = manager->priv;
    if (!priv->context) {
        g_set_error_literal(err, SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                            _("libusb backend is null"));
        return NULL;
    }

    return spice_usb_backend_allocate_device_for_file_descriptor(priv->context,
                                                                 file_descriptor,
                                                                 err);
#else
    g_set_error_literal(err, SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                        _("USB redirection support not compiled in"));
    return NULL;
#endif
}

#ifdef USE_USBREDIR
/*
 * SpiceUsbDevice
 */
static SpiceUsbDevice *spice_usb_device_new(SpiceUsbDevice *bdev)
{
    g_return_val_if_fail(bdev != NULL, NULL);

    return spice_usb_backend_device_ref(bdev);
}

guint16 spice_usb_device_get_busnum(const SpiceUsbDevice *info)
{
    const UsbDeviceInformation *b_info;

    g_return_val_if_fail(info != NULL, 0);

    b_info = spice_usb_backend_device_get_info(info);
    return b_info->bus;
}

guint8 spice_usb_device_get_devaddr(const SpiceUsbDevice *info)
{
    const UsbDeviceInformation *b_info;

    g_return_val_if_fail(info != NULL, 0);

    b_info = spice_usb_backend_device_get_info(info);
    return b_info->address;
}

guint16 spice_usb_device_get_vid(const SpiceUsbDevice *info)
{
    const UsbDeviceInformation *b_info;

    g_return_val_if_fail(info != NULL, 0);

    b_info = spice_usb_backend_device_get_info(info);
    return b_info->vid;
}

guint16 spice_usb_device_get_pid(const SpiceUsbDevice *info)
{
    const UsbDeviceInformation *b_info;

    g_return_val_if_fail(info != NULL, 0);

    b_info = spice_usb_backend_device_get_info(info);
    return b_info->pid;
}

gboolean spice_usb_device_is_isochronous(const SpiceUsbDevice *info)
{
    g_return_val_if_fail(info != NULL, 0);

    return spice_usb_backend_device_isoch((SpiceUsbDevice*) info);
}

#ifdef G_OS_WIN32
static
gboolean _usbdk_hider_prepare(SpiceUsbDeviceManager *manager)
{
    SpiceUsbDeviceManagerPrivate *priv = manager->priv;

    if (priv->usbdk_api == NULL) {
        return FALSE;
    }

    if (priv->usbdk_hider_handle == NULL) {
        priv->usbdk_hider_handle = usbdk_create_hider_handle(priv->usbdk_api);
        if (priv->usbdk_hider_handle == NULL) {
            g_warning("Failed to instantiate UsbDk hider interface");
            return FALSE;
        }
    }

    return TRUE;
}

static
void _usbdk_hider_clear(SpiceUsbDeviceManager *manager)
{
    SpiceUsbDeviceManagerPrivate *priv = manager->priv;

    if (priv->usbdk_api == NULL) {
        return;
    }

    if (priv->usbdk_hider_handle != NULL) {
        usbdk_clear_hide_rules(priv->usbdk_api, priv->usbdk_hider_handle);
        usbdk_close_hider_handle(priv->usbdk_api, priv->usbdk_hider_handle);
        priv->usbdk_hider_handle = NULL;
    }
}

static
void _usbdk_hider_update(SpiceUsbDeviceManager *manager)
{
    SpiceUsbDeviceManagerPrivate *priv = manager->priv;

    if (priv->usbdk_api == NULL) {
        return;
    }

    if (priv->auto_connect_filter == NULL) {
        SPICE_DEBUG("No autoredirect rules, no hider setup needed");
        _usbdk_hider_clear(manager);
        return;
    }

    if (!spice_session_get_usbredir_enabled(priv->session)) {
        SPICE_DEBUG("USB redirection disabled, no hider setup needed");
        _usbdk_hider_clear(manager);
        return;
    }

    if (!priv->auto_connect) {
        SPICE_DEBUG("Auto-connect disabled, no hider setup needed");
        _usbdk_hider_clear(manager);
        return;
    }

    if (_usbdk_hider_prepare(manager)) {
        usbdk_api_set_hide_rules(priv->usbdk_api,
                                 priv->usbdk_hider_handle,
                                 priv->auto_connect_filter);
    }
}

#endif

static SpiceUsbDevice *spice_usb_device_ref(SpiceUsbDevice *info)
{
    g_return_val_if_fail(info != NULL, NULL);
    return spice_usb_backend_device_ref(info);
}

static void spice_usb_device_unref(SpiceUsbDevice *info)
{
    g_return_if_fail(info != NULL);

    spice_usb_backend_device_unref(info);
}

static gboolean
spice_usb_manager_device_equal_bdev(SpiceUsbDeviceManager *manager,
                                    SpiceUsbDevice *info,
                                    SpiceUsbDevice *bdev)
{
    if ((info == NULL) || (bdev == NULL)) {
        return FALSE;
    }

    return info == bdev;
}
#endif /* USE_USBREDIR */
