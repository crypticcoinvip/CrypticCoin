package=libcap
$(package)_version=2.25
$(package)_download_path=http://http.debian.net/debian/pool/main/libc/libcap2
$(package)_file_name=$(package)2_$($(package)_version).orig.tar.xz
$(package)_sha256_hash=693c8ac51e983ee678205571ef272439d83afe62dd8e424ea14ad9790bc35162

define $(package)_build_cmds
  $(MAKE) -j$(nproc)
endef

define $(package)_stage_cmds
  mkdir -p $($(package)_staging_dir)$(host_prefix)/lib/ && \
  cp -rf $($(package)_build_subdir)/libcap/libcap.a $($(package)_staging_dir)$(host_prefix)/lib/ && \
  cp -rf $($(package)_build_subdir)/libcap/include $($(package)_staging_dir)$(host_prefix)
endef

