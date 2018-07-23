package=libseccomp
$(package)_version=2.3.3
$(package)_download_path=https://github.com/seccomp/libseccomp/releases/download/v$($(package)_version)
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=7fc28f4294cc72e61c529bedf97e705c3acf9c479a8f1a3028d4cd2ca9f3b155

define $(package)_set_vars
  $(package)_config_opts += --enable-static=yes
  $(package)_config_opts += --enable-shared=no
endef

define $(package)_preprocess_cmds
	cd $($(package)_build_subdir) && \
	./configure $($(package)_config_opts) --libdir=$(host_prefix)/lib --includedir=$(host_prefix)/include
endef

define $(package)_build_cmds
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef

