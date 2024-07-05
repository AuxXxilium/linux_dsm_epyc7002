
require 'syno_kconfig'

describe 'CONFIG_SYNO_TTY_FIX_TTYS_FUNCTIONS' do
	include SynoKconfig

	it "CONFIG_SYNO_TTY_FIX_TTYS_FUNCTIONS=y if not virtual or arm platforms" do
		platforms
			.reject { |p| p.virtual? || p.aarch64? }
			.verify(desc, builtin?)

		platforms
			.select { |p| p.virtual? || p.aarch64? }
			.verify(desc, disabled?)
	end
end
