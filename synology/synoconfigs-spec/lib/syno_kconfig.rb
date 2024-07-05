
require 'platform'
require 'ansi/code'
require 'minitest/spec'
require 'minitest/assertions'
require 'minitest/expectations'
require 'minitest/autorun'
require 'minitest/reporters'
require 'minitest/hooks/default'

Minitest::Reporters.use! [Minitest::Reporters::SpecReporter.new]

# monkey patch without refinement
class Array
    def verify(config, closure = nil, hook = ->(p,c){ Minitest::Spec.current.cover(p,c) })
        msg = ""
        enum = each do |platform|
            begin
                if block_given?
                    yield(platform, config)
                elsif closure
                    closure.call(platform, config)
                end
            rescue Minitest::Assertion => e
                msg << %Q[#{platform} >>  #{e.message.strip.gsub("\n", "")}\n]
            else
                hook[platform, config]
            end
        end

        raise Minitest::Assertion, msg unless msg.empty?
        enum
    end
end

class String
    def enabled?
        self == 'y' || self == 'm'
    end
end

def nil.enabled?
    false
end

module SynoKconfig
    def self.included(klass)
        klass.instance_eval do
            before(:all) do
                @noop = ->(_,_) { }
            end

            after(:all) do
                self.class.coverage.each do |cfg, plats|
                    plats.uniq!
                    if plats.size != platforms.size
                        puts
                        puts "  Coverage: #{plats.size}/#{platforms.size}. Not Covered: "
                        puts "    #{(platforms.collect(&:abbr) - plats).join(', ')}"
                        puts
                        flunk "#{cfg} isn't covered in all platforms."
                    end
                end
            end

            def configs
                @configs ||=
                    Platform::load.inject([]) { |c,p| c + p.configs.keys }.uniq
            end

            def coverage
                @coverage ||= Hash.new { |h,k| h[k] = [] }
            end

            def scope(pattern)
                configs
                    .select { |cfg| cfg =~ pattern }
                    .each   { |cfg| coverage[cfg] }
            end
        end
    end

    def necessary
        @noop
    end

    def method_missing(method_name, *args)
        assert(false, "#{method_name} not found")
    end

    def builtin?
        ->(p, c) { assert_equal(p[c], 'y') }
    end

    def module?
        ->(p, c) { assert_equal(p[c], 'm') }
    end

    def enabled?
        ->(p, c) { assert_includes(['y', 'm'], p[c]) }
    end

    def disabled?
        ->(p, c) { assert_nil(p[c]) }
    end

    def equaled?(value)
        ->(p, c) { assert_equal(p[c], value) }
    end

    def desc
        self.class.desc
    end

    # only platform that meet the sufficient conditions could be covered
    def cover(platform, config)
        self.class.coverage[config] << platform.abbr
    end

    def platforms
        @platforms ||= Platform::load
    end

end

