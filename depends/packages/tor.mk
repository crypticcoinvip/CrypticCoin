package=tor
# $(package)_version=0.3.2.9
# $(package)_sha256_hash=d1e621e716bec097d1094c34738bceb817b7f924d7d65f0cf835be3a9f7c4394
# $(package)_file_name=$(package)-$($(package)_version).tar.gz
# $(package)_download_path=https://github.com/torproject/tor/archive
$(package)_version=0.3.3.9
$(package)_sha256_hash=d98991212edfda5b9ce46f7c8a92eecee06de3a8d48b702601bceb6f24610adb
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_download_path=https://github.com/torproject/tor/archive
$(package)_dependencies=libseccomp libcap libevent zlib openssl

define $(package)_set_vars
  $(package)_config_opts += --disable-unittests
  $(package)_config_opts += --disable-system-torrc
  $(package)_config_opts += --disable-systemd
  $(package)_config_opts += --disable-lzma
  $(package)_config_opts += --disable-asciidoc
  $(package)_config_opts += --enable-static-tor
  $(package)_config_opts += --prefix=$(host_prefix)
  $(package)_config_opts += --with-libevent-dir=$(host_prefix)
  $(package)_config_opts += --with-openssl-dir=$(host_prefix)
  $(package)_config_opts += --with-zlib-dir=$(host_prefix)
endef

define $(package)_preprocess_cmds
  tar xf $(libevent_cached) -C $(host_prefix) && \
  tar xf $(openssl_cached) -C $(host_prefix) && \
  tar xf $(zlib_cached) -C $(host_prefix) && \
  cd $($(package)_build_subdir) && \
  patch --no-backup-if-mismatch -f -p0 < $(PATCHES_PATH)/$(package)/configure_ac.patch && \
  ./autogen.sh && \
  ./configure $($(package)_config_opts)
endef

define $(package)_build_cmds
  $(MAKE) LDFLAGS="-L$(host_prefix)/lib"
endef

define $(package)_stage_cmds
  mkdir -p $($(package)_staging_dir)$(host_prefix)/lib/ && \
  cp $($(package)_build_dir)/src/or/libtor.a $($(package)_staging_dir)$(host_prefix)/lib/ && \
  cp $($(package)_build_dir)/src/common/libor.a $($(package)_staging_dir)$(host_prefix)/lib/ && \
  cp $($(package)_build_dir)/src/common/libor-ctime.a $($(package)_staging_dir)$(host_prefix)/lib/ && \
  cp $($(package)_build_dir)/src/common/libor-crypto.a $($(package)_staging_dir)$(host_prefix)/lib/ && \
  cp $($(package)_build_dir)/src/common/libor-event.a $($(package)_staging_dir)$(host_prefix)/lib/ && \
  cp $($(package)_build_dir)/src/trunnel/libor-trunnel.a $($(package)_staging_dir)$(host_prefix)/lib/ && \
  cp $($(package)_build_dir)/src/common/libcurve25519_donna.a $($(package)_staging_dir)$(host_prefix)/lib/ && \
  cp $($(package)_build_dir)/src/ext/ed25519/donna/libed25519_donna.a $($(package)_staging_dir)$(host_prefix)/lib/ && \
  cp $($(package)_build_dir)/src/ext/ed25519/ref10/libed25519_ref10.a $($(package)_staging_dir)$(host_prefix)/lib/ && \
  cp $($(package)_build_dir)/src/ext/keccak-tiny/libkeccak-tiny.a $($(package)_staging_dir)$(host_prefix)/lib/
endef