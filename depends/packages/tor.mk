package=tor
$(package)_version=0.3.2.10
$(package)_sha256_hash=08c5207e59de0bc3410ceb2743731750daad9b5e296f79d87c237178419ceebb
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_download_path=https://github.com/torproject/tor/archive

define $(package)_preprocess_cmds
  cd $($(package)_build_subdir); ./autogen.sh && ./configure --disable-asciidoc
endef

define $(package)_build_cmds
  $(MAKE) -j2
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef