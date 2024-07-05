
require 'syno_kconfig'

describe 'CONFIG_SYNO_EXT4_*' do
    include SynoKconfig

    # all configs with this prefix should be examined
    scope(/^CONFIG_SYNO_EXT3_/)
    scope(/^CONFIG_SYNO_EXT4_/)

    # built-in configs
    %w[
        CONFIG_EXT2_FS
        CONFIG_EXT2_FS_XATTR
        CONFIG_EXT3_FS
        CONFIG_EXT4_FS
        CONFIG_EXT4_FS_SECURITY
        CONFIG_JBD2
    ].each do |cfg|
        it "#{cfg}=y" do
            platforms.verify(cfg, builtin?)
        end
    end

    # disabled configs
    %w[
        CONFIG_EXT2_FS_POSIX_ACL
        CONFIG_EXT2_FS_SECURITY
        CONFIG_EXT3_FS_POSIX_ACL
        CONFIG_EXT3_FS_SECURITY
        CONFIG_EXT4_FS_POSIX_ACL
        CONFIG_EXT4_DEBUG
    ].each do |cfg|
        it "#{cfg} is not set" do
            platforms.verify(cfg, disabled?)
        end
    end

    configs
        .select { |cfg| cfg =~ /^CONFIG_SYNO_EXT3_/ }
        .each do |cfg|
            it "#{cfg}=y" do
                platforms.verify(cfg, builtin?)
            end
        end

    configs
        .select { |cfg| cfg =~ /^CONFIG_SYNO_EXT4_/ }
        .reject { |cfg| cfg == 'CONFIG_SYNO_EXT4_LAZYINIT_WAIT_MULT' }
        .each do |cfg|
            it "#{cfg}=y" do
                platforms.verify(cfg, builtin?)
            end
        end

    it "CONFIG_SYNO_EXT4_LAZYINIT_WAIT_MULT=2" do
        platforms
            .verify('CONFIG_SYNO_EXT4_LAZYINIT_WAIT_MULT', equaled?(2))
    end
end

