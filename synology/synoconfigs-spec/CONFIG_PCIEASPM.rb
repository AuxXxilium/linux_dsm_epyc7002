
require 'syno_kconfig'

describe 'CONFIG_PCIEASPM' do
	include SynoKconfig

	# builtin configs
	%w[
		CONFIG_PCIEASPM
		CONFIG_PCIEASPM_PERFORMANCE
	].each do |cfg|
		it "#{cfg}=y" do
			platforms.verify(cfg, builtin?)
		end
	end

	# disable configs
	%w[
		CONFIG_PCIEASPM_DEFAULT
		CONFIG_PCIEASPM_POWERSAVE
		CONFIG_PCIEASPM_POWER_SUPERSAVE
	].each do |cfg|
		it "#{cfg} is not set" do
			platforms.verify(cfg, disabled?)
		end
	end
end

