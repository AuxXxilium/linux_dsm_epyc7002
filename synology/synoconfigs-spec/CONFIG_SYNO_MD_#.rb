
require 'syno_kconfig'

describe 'CONFIG_SYNO_MD_*' do
    include SynoKconfig

    # FIXME: do we need MD in kvmx64 ?

    # all configs with this prefix should be examined
    scope(/^CONFIG_SYNO_MD_/)

    # kernel module
    %w[
        CONFIG_MD_LINEAR
        CONFIG_MD_RAID10
        CONFIG_MD_RAID456
    ].each do |cfg|
        it "#{cfg}=m" do
            platforms.verify(cfg, module?)
        end
    end

    # built-in configs
    %w[
        CONFIG_MD
        CONFIG_BLK_DEV_MD
        CONFIG_MD_AUTODETECT
        CONFIG_MD_RAID1
    ].each do |cfg|
        it "#{cfg}=y" do
            platforms.verify(cfg, builtin?)
        end
    end

    configs
        .select { |cfg| cfg =~ /^CONFIG_SYNO_MD_/ }
        .reject { |cfg| cfg == 'CONFIG_SYNO_MD_NUMA_SETTING_ENHANCE' }
        .reject { |cfg| cfg == 'CONFIG_SYNO_MD_DM_CRYPT_QUEUE_LIMIT'}
        .each do |cfg|
            it "#{cfg}=y" do
                platforms.verify(cfg, builtin?)
            end
        end

    it "CONFIG_SYNO_MD_NUMA_SETTING_ENHANCE=y if CONFIG_NUMA enabled" do
        platforms
            .select { |p| p['CONFIG_NUMA'].enabled? }
            .verify('CONFIG_SYNO_MD_NUMA_SETTING_ENHANCE', enabled?)

        platforms
            .reject { |p| p['CONFIG_NUMA'].enabled? }
            .verify('CONFIG_SYNO_MD_NUMA_SETTING_ENHANCE', disabled?)
    end

    it "CONFIG_SYNO_MD_DM_CRYPT_QUEUE_LIMIT=y if CONFIG_DM_CRYPT enabled" do
        platforms
            .select { |p| p['CONFIG_DM_CRYPT'].enabled? }
            .verify('CONFIG_SYNO_MD_DM_CRYPT_QUEUE_LIMIT', enabled?)

        platforms
            .reject { |p| p['CONFIG_DM_CRYPT'].enabled? }
            .verify('CONFIG_SYNO_MD_DM_CRYPT_QUEUE_LIMIT', disabled?)
    end
end

