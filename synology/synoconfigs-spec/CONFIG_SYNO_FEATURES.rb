
require 'syno_kconfig'

describe 'CONFIG_SYNO_FEATURES' do
    include SynoKconfig

    # you can use variable 'desc' if your description
    # is idential to CONFIG name to be tested.
    it "#{desc}=y" do
        platforms.verify(desc, builtin?)
    end
end

