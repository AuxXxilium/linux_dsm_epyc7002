
require 'syno_kconfig'

describe 'CONFIG_SYNO_HFSPLUS_*' do
    include SynoKconfig

    # all configs with this prefix should be examined
    scope(/^CONFIG_SYNO_HFSPLUS_/)

    configs
        .select { |cfg| cfg =~ /^CONFIG_SYNO_HFSPLUS_/ }
        .each do |cfg|
            it "#{cfg}=y" do
                platforms.verify(cfg, builtin?)
            end
        end
end

