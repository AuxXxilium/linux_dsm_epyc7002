
require 'syno_kconfig'

describe 'CONFIG_CRYPTO_*' do
    include SynoKconfig

    %w[
        CONFIG_CRYPTO_CRC32C
        CONFIG_LIBCRC32C
    ].each do |cfg|
        it "#{cfg}=y" do
            platforms.verify(cfg, builtin?)
        end
    end

    it "CONFIG_CRYPTO_CRC32C_INTEL=y if x86_64 platforms" do
        platforms
            .select { |p| p.family == :x86_64 }
            .verify('CONFIG_CRYPTO_CRC32C_INTEL', builtin?)
    end

    it "CONFIG_CRYPTO_CRC32C_INTEL is not set if !x86_64 platforms" do
        platforms
            .reject { |p| p.family == :x86_64 }
            .verify('CONFIG_CRYPTO_CRC32C_INTEL', disabled?)
    end
end

