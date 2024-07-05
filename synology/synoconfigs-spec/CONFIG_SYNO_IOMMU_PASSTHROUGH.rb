
require 'syno_kconfig'

describe 'CONFIG_SYNO_IOMMU_PASSTHROUGH' do
	include SynoKconfig

	it "CONFIG_SYNO_IOMMU_PASSTHROUGH=y if not virtual platforms" do
		platforms
			.reject { |p| p.virtual? }
			.verify(desc, builtin?)

		platforms
			.select { |p| p.virtual? }
			.verify(desc, disabled?)
	end
end
