
require 'syno_kconfig'

describe 'CONFIG_SYNO_SCSI_OVERRIDE_SD_TIMEOUT' do
	include SynoKconfig

	it "CONFIG_SYNO_SCSI_OVERRIDE_SD_TIMEOUT=y if CONFIG_ATA | CONFIG_SYNO_SAS enabled" do
		platforms
			.select { |p| p['CONFIG_ATA'].enabled? }
			.verify('CONFIG_SYNO_SCSI_OVERRIDE_SD_TIMEOUT', enabled?)

		platforms
			.select { |p| p['CONFIG_SYNO_SAS'].enabled? }
			.verify('CONFIG_SYNO_SCSI_OVERRIDE_SD_TIMEOUT', enabled?)

		platforms
			.reject { |p| p['CONFIG_ATA'].enabled? }
			.reject { |p| p['CONFIG_SYNO_SAS'].enabled? }
			.verify('CONFIG_SYNO_SCSI_OVERRIDE_SD_TIMEOUT', disabled?)
    end
end

