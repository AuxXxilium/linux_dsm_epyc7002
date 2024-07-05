
require 'syno_kconfig'

describe 'CONFIG_SWIOTLB' do
	include SynoKconfig

	it "CONFIG_SWIOTLB=y if CONFIG_64BIT enabled" do
		platforms
			.select { |p| p['CONFIG_64BIT'].enabled? }
			.verify(desc, enabled?)

		platforms
			.reject { |p| p['CONFIG_64BIT'].enabled? }
			.verify(desc, disabled?)
	end
end

