How to do a SPICE protocol release
==================================

Some notes to prepare a release, not really strict but better to have in order
to avoid forgetting something.

* Check `meson.build` and `spice-protocol.spec` for release number
* Update `CHANGELOG.md` with list of changes done since last release
* Send a merge request with such changes, handle the review
* Build distribution file with `meson dist`
* Try to build an RPM package from `meson dist` output
* `git push` for the above MR
* Create a git tag (like `git tag -a v0.14.3 -m 'Release v0.14.3'`)
* `git push` for the version tag created (for instance you can use
  `git push origin v0.14.3` or `git push --tags`)
* Sign generated tarball (to create a detached signature run
  `gpg2 -sb spice-protocol-RELEASE.tar.xz`)
* On Gitlab update tags (https://gitlab.freedesktop.org/spice/spice-protocol/-/tags)
  * Add ChangeLog information
  * Upload tarball and relative signature
* Upload tarball and relative signature and sha256sum to
  https://www.spice-space.org/download/releases/ (sftp to
  `spice-uploader@spice-web.osci.io:/var/www/www.spice-space.org/download/releases/`)
* Update file `download.rst` in
  https://gitlab.freedesktop.org/spice/spice-space-pages
* Send an email to spice-devel mailing list
* Bump version post release and send a new MR
