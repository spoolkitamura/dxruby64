version = RUBY_VERSION.split('.')[0..1].join   # e.g. "3.3.8" -> "33"
require_relative "#{version}/dxruby"
