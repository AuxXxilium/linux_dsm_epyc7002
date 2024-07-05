
require 'syno_kconfig'

describe 'CONFIG_SYNO_USB_*' do
	include SynoKconfig

	# builtin configs
	%w[
		CONFIG_SYNO_USB_VBUS_GPIO_CONTROL
		CONFIG_SYNO_USB_POWER_RESET
	].each do |cfg|
		it "#{cfg}=y if not virtual platforms" do
			platforms
				.reject { |p| p.virtual? }
				.verify(cfg, builtin?)

			platforms
				.select { |p| p.virtual? }
				.verify(cfg, disabled?)
		end
	end

	%w[
		CONFIG_SYNO_USB_INTEL_XHC_LPM_DISABLE
	].each do |cfg|
		it "#{cfg}=y if not virtual or arm64 platforms" do
			platforms
				.reject { |p| p.virtual? || p.aarch64? }
				.verify(cfg, builtin?)

			platforms
				.select { |p| p.virtual? || p.aarch64? }
				.verify(cfg, disabled?)
		end
	end

end

