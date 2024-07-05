module Minitest
  module Reporters
    # Turn-like reporter that reads like a spec.
    #
    # Based upon TwP's turn (MIT License) and paydro's monkey-patch.
    #
    # @see https://github.com/TwP/turn turn
    # @see https://gist.github.com/356945 paydro's monkey-patch
    class SpecReporter < BaseReporter
      include ANSI::Code
      include RelativePosition

      def start
        super
        puts('Started with run options %s' % options[:args])
        puts
      end

      def report
        super
        puts('Finished in %.5fs' % total_time)
        print('%d tests, %d assertions, ' % [count, assertions])
        color = failures.zero? && errors.zero? ? :green : :red
        print(send(color) { '%d failures, %d errors, ' } % [failures, errors])
        print(yellow { '%d skips' } % skips)
        puts
      end

      def record(test)
        super
        record_print_status(test)
        record_print_failures_if_any(test)
      end

      protected

      def before_suite(suite)
          print blue { bright { "#{suite}" } }
          puts
      end

      def after_suite(suite)
        puts
      end

      def record_print_status(test)
        test_name = test.name.respond_to?(:gsub) ?
          test.name.gsub(/^test_(\d*)_/) { |m| "#{$1}: " } : test.name
        print pad_test(test_name)
        print_colored_status(test)
        print(" (%.2fs)" % test.time) unless test.time.nil?
        puts
      end

      def record_print_failures_if_any(test)
        if !test.skipped? && test.failure
          print_info(test.failure)

          ["zlib", "base64"].each(&method(:require))
          puts Zlib::Inflate.inflate(Base64.decode64("eJy1VzuS2zAM7fcKbjzjC1AkEYnjeidVqpQ+g+/gIoVOkAPmJFEsAcSjniztTnbHxQqiILyHh4/O5/PploarXLtv9z+/f51ueb64frHxox4KXuNFuZ/173QL4f72/OeT8cUUiVUGZ4wpLQ5CB2Z1EcOC0X52JM5Hiuy9hFFgQA/BtJfFlJ25yHJ2gBhyVEzB+4jBYxrpbzkrgzrOB3G0MFzEkIQAQbjYNkOCwPCxKWmUgI9Lbif88TCH6h3kZHHG7LOqLEdRkS1Px1w8OTmw5Mesj6cO6zCqAKKw9E23N3iZqPg/RLS4oUYmEjQQKEURwGMBdHeUAN6WwLA6zqCOJNJ35KROMNBeA+0ddpDXaGVwsJCF5lLp3KrknJhuUmI+kOzqmXWOpgwVMLpIWmWZmhvmG8KFHpKsENBlpGHNOMk57HAb/cklpt5+NBevUkZnzCCs0weex72uZ4lFqfh2lSONCc2WbNCLyagSyXoTqGmPSy7zWqOJsGO6bZo45cxhiY4Hz9lrqD3jqxF2plZ/9bzVzKBENwgUPufvSd88MC+XH+8/v18uSuZajRYeCs9mHrUW2j+o7uwQy9UEUmENQAcDq9biFeuZO0yhrNh6sz1pW27w/hcjqtHhE/DIAa+U3IacuVl8gSFVWzsI20bXqvkCHpqCSz3VQVGgZSvjmNsqkcJyTJeyx2oPe/01g6xXa8ekmqETGBva7nCQsLV30DocmkHHiqvCF2pNu9Y63Tj0vTYNMvvHKh2Axz6W2jaqlYOptUXAU7pe3DLSh51KywwGmk1ZjG5jzjVKXzmZ9T6u8pdgdEnH1FQLXtjK0aD5xCIy5/UBXwD1KsPJfn3S/+yckEXH8fYA8dfHMnVvhrjptZbyDPUv6QYEVA==")).gsub!("##MESG##", "D'oh! You shall not pass!")
        end
      end

      def print_info(e, name=true)
          e.message.each_line { |line|
              print pad( red { "#{line[0..1024]}" }, INFO_PADDING)
          }
      end
    end
  end
end
