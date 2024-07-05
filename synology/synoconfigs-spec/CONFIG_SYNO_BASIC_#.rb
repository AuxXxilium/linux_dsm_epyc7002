
require 'syno_kconfig'

describe 'CONFIG_SYNO Basic' do
    include SynoKconfig

    #
    # built-in configs for all platforms
    #
    %w[
        CONFIG_SYNO_SYSTEM_CALL
        CONFIG_SYNO_LIBS
        CONFIG_SYNO_KWORK_STAT
        CONFIG_SYNO_EXPORT_SYMBOL
        CONFIG_SYNO_SWAP_FLAG
        CONFIG_SYNO_DATA_CORRECTION
        CONFIG_SYNO_DISPLAY_CPUINFO
        CONFIG_SYNO_LOAD_AVERAGE
    ].each do |cfg|
        it "#{cfg}=y" do
            platforms.verify(cfg, builtin?)
        end
    end
end

