Major changes in 0.14.5
=======================
* Add SPICE_MSG_DISPLAY_GL_SCANOUT2_UNIX
* Fix for Windows Arm64 build

Major changes in 0.14.4
=======================
* Fix enum deprecation warning for visual studio
* Fix documentation typos in stream-device.h

Major changes in 0.14.3
=======================
* Add VD_AGENT_CLIPBOARD_FILE_LIST to support copy/paste of files with
  WebDAV support
* Add support for side mouse buttons
* Add a MonitorsMM field to VDAgentMonitorsConfig allowing to pass
  physical monitor dimension

Major changes in 0.14.2
=======================
* Removed Autoconf support, only Meson is available
* Removed foreign-menu and controller interfaces
* Deprecated CELT support
* Generate MingW packages building the RPM
* Allows for the agent to pass back a more specific error to the client
* Add quality indicators messages

Major changes in 0.14.1
=======================
* This is the last release with:
  * Autotools: Meson is now the only supported
  * foreign_menu_prot.h: Deprecated in this release
  * controller_prot.h: Deprecated in this release
* Add VD_AGENT_CAP_CLIPBOARD_NO_RELEASE_ON_REGRAB
* Add VD_AGENT_CAP_CLIPBOARD_GRAB_SERIAL
* Add SPICE_MSGC_MAIN_QUALITY_INDICATOR enum
* Add STREAM_TYPE_QUALITY_INDICATOR message
* Remove deprecated vdi_dev.h interface
* Remove deprecated VD_AGENT_CLIPBOARD_MAX_SIZE_DEFAULT
* Remove deprecated VD_AGENT_CLIPBOARD_MAX_SIZE_ENV
* Remove unused SPICE_GNUC_ macros
* qxl_dev: QXLReleaseInfo alignment fix
* Some documentation added in vd_agentd.h
* Now shipping a rpm spec file to easy deployment and testing

Major changes in 0.14.0
=======================
* Bumping minor to show that some types and values were removed
* Regenerate enums.h (from spice.proto) which removes:
 * SpicePubkeyType;
 * SpiceTunnelServiceType;
 * SpiceTunnelIpType;
 * SPICE_MSG_TUNNEL_* and SPICE_MSGC_TUNNEL_*
* Add VDAgentGraphicsDeviceInfo message
* Add StreamMsgGraphicsDeviceInfo message
* Add padding to SpiceStat structure

Major changes in 0.12.15
========================
* Add support for h265 video codec
* qxl_dev: Align QXLRam to 4 bytes
* meson: fix spice-protocol as subproject

Major changes in 0.12.14
========================
* add stream-device protocol
* add SPICE_SURFACE_FLAGS_STREAMING_MODE flag
* add e2k (Elbrus 2000) architecture
* add SPICE_MAX_NUM_STREAMS
* add meson support
* add bult-ins byte swapping for Visual C++
* switch to built-ins byte swapping in GCC

Major changes in 0.12.13
========================
* add DISPLAY_PREFERRED_VIDEO_CODEC_TYPE
* add VP9 codec type
* add VD_AGENT_CLEAR_CAPABILITY() macro
* add VD_AGENT_CAP_FILE_XFER_DISABLED
* add VD_AGENT_FILE_XFER_STATUS_NOT_ENOUGH_SPACE
* add new file-xfer statuses for detailed error
* Change enums.h license to MIT

Major changes in 0.12.12
========================
* protocol: Add lz4 compression support to the SpiceVMC channel

Major changes in 0.12.11
========================
* protocol: add support for the VP8 and h264 video codecs
* protocol: add unix GL scanout messages
* remove code generation scripts, moved back to spice-common
* macros improvements, more type safety

Major changes in 0.12.10
========================
* Add VD_AGENT_CAP_MONITORS_CONFIG_POSITION to handle multi-monitor
  configurations that are not multi-head
* Add protocol code generation scripts from spice-common
* Endianness and compiler portability fixes (clang)

Major changes in 0.12.9
=======================
* rename newly introduced SpiceImageCompress enum to SpiceImageCompression
  as otherwise it was clashing with the definition used by older spice-server
  releases, breaking QEMU build

Major changes in 0.12.8
=======================
* add LZ4 support
* add audio volume synchronization
* deprecate unused vdi-dev
* add 'preferred-compression' message/capability
* add a new Windows driver escape code to send
  monitors capability from guest to client

Major changes in 0.12.7
=======================
* add support for Webdav channel
* add support for the Opus codec

Major changes in 0.12.6
=======================
* add adaptive video streaming support:
  control playback latency and receive playback
  reports from the client.
* add agent capabilities for signaling guest line ending.

Major changes in 0.12.5
=======================
* Add agent file xfer success status
* Add a client-disconnected agent message

Major changes in 0.12.4
=======================
* Add agent file copy support.
* Add agent sparse monitors capability.
* Add controller proxy message.

Major changes in 0.12.3
=======================
* Add a generic "port" channel

Major changes in 0.12.2
=======================
* Add A8 surface capability in display channel.
* Add to qxl device support for:
 * client present
 * client capabilities
 * client monitors configuration

Major changes in 0.12.1
=======================
* Support seamless migration.
* New QXLComposite message for better X support.
* Support arbitrary scancode message INPUTS_KEY_SCANCODE.

Major changes in 0.12.0
=======================
* Add support for arbitrary resolution on Windows QXL with
  QXL_ESCAPE_SET_CUSTOM_DISPLAY
* Add support for arbitrary resolution and multiple monitor per
  display channel with QXLMonitorsConfig and co
* build cleanup

Major changes in 0.10.3 (0.10.2 was never released)
===================================================
* Add support for video streams with differently sized (wxh) data
* Add controller messages for USB redirection, COLOR_DEPTH, DISABLE_EFFECTS,
  and ENABLE_SMARTCARD
* Add name & uuid messages on main channel
* some cleanups
* Fixes RHBZ#815422, RHBZ#807295, RHBZ#787447

Major changes in 0.10.1
=======================
* Add support for a header without sublist and serial (mini header)

Major changes in 0.10.0
=======================
* no changes, released to match version with spice

Major changes in 0.9.1 (same as 0.8.2 in 0.8 branch)
======================
* Add support for semi-seamless migration

Major changes in 0.9.0
======================
* Add support for generic spicevmc chardev passthrough messages
* Add USB redirection channel

Major changes in 0.8.1
======================
* Add support for volume change
* Add support for async guest io writes and interrupt
* Add support for suspend related guest io writes
* Add support for interrupt indicating guest bug

Major changes in 0.8.0
======================
* Add support for different clipboards (selections) to vd_agent copy paste
* Add support for using different authentication mechanisms (just SASL for now)

Major changes in 0.7.1
======================
* Add some enums for the xorg qxl driver
* Some other small fixes

Major changes in 0.7.0
======================
* Add smartcard channel

Major changes in 0.6.4
======================
* Make controller client protocol menu text UTF8 rather then 16 bit unicode

Major changes in 0.6.3:
=======================
* Add support for copy and paste to the agent protocol
* Add foreign-menu and external controller client protocol headers

Major changes in 0.6.2:
=======================
* Skipped to stay in sync with spice

Major changes in 0.6.1:
=======================
* Added compat flag for 16bpp commands

Major changes in 0.6.0:
=======================
* Initial messages for clipboard sharing
* Move agent protocol structs from spice to spice-protocol
* Add capabilities to agent protocol

Major changes in 0.5.3:
=======================

Network major number changed to 2 to reflect that the network
protocol is now stable and backwards compatible from this point.

Some vdagent messages for display settings and clipboard sharing
were added.

Major changes in 0.5.2:
=======================

This is the first release of the unstable 0.5.x series leading up to 0.6.
This module was split out of spice so that e.g. drivers and qemu can
get the types and constants they need without using the full spice
codebase.
