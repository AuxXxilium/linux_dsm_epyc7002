
require 'syno_kconfig'

describe 'CONFIG_NUMA' do
    include SynoKconfig

    # you can use variable 'desc' if your description
    # is idential to CONFIG name to be tested.
    it "#{desc}=y for NUMA platforms" do
        platforms
            .select { |p| p.numa? }
            .verify(desc, builtin?)

        platforms
            .reject { |p| p.numa? }
            .verify(desc, disabled?)
    end
end

