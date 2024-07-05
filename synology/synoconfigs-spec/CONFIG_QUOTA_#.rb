
require 'syno_kconfig'

describe 'CONFIG_QUOTA_*' do
    include SynoKconfig

    # all configs with this prefix should be examined
    scope(/^CONFIG_QUOTA/)
    scope(/^CONFIG_QFMT/)
    scope(/^CONFIG_QUOTACTL/)

    it "CONFIG_QUOTA=y" do
        platforms.verify("CONFIG_QUOTA", builtin?)
    end

    it "CONFIG_QUOTA_DEBUG is not set" do
        platforms.verify("CONFIG_QUOTA_DEBUG", disabled?)
    end

    it "CONFIG_QUOTA_TREE=y" do
        platforms.verify("CONFIG_QUOTA_TREE", builtin?)
    end

    it "CONFIG_QFMT_V1 is not set" do
        platforms.verify("CONFIG_QFMT_V1", disabled?)
    end

    it "CONFIG_QFMT_V2=y" do
        platforms.verify("CONFIG_QFMT_V2", builtin?)
    end

    it "CONFIG_QUOTACTL=y" do
        platforms.verify("CONFIG_QUOTACTL", builtin?)
    end
end

