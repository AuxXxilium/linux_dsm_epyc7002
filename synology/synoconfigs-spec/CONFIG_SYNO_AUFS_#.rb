
require 'syno_kconfig'

describe 'CONFIG_SYNO_AUFS_*' do
    include SynoKconfig

    # all configs with this prefix should be examined
    scope(/^CONFIG_SYNO_AUFS_/)

    configs
        .select { |cfg| cfg =~ /^CONFIG_SYNO_AUFS_/ }
        .each do |cfg|
            it "#{cfg}=y" do
                platforms.verify(cfg, builtin?)
            end
        end
end

