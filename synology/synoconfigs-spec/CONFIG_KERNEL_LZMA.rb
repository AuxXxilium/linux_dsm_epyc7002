
require 'syno_kconfig'

describe 'CONFIG_KERNEL_LZMA' do
	include SynoKconfig

	it "CONFIG_KERNEL_LZMA=y if not arm platforms" do
		platforms
			.reject { |p| p.aarch64? }
			.verify(desc, builtin?)

		platforms
			.select { |p| p.aarch64? }
			.verify(desc, disabled?)
	end
end
