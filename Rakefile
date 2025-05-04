# frozen_string_literal: true

require "rubygems"
require "bundler/setup"
require "rake"
require "fileutils"

include FileUtils

GEMSPEC = "dxruby64.gemspec"
GEM_VERSION = Gem::Specification.load(GEMSPEC).version.to_s
GEM_FILE = "pkg/dxruby64-#{GEM_VERSION}.gem"

# --- Gemのビルド ---
desc "Build the gem"
task :build do
  # Gemをビルドして、pkg/に保存
  sh "gem build #{GEMSPEC}"

  # カレントに作られた .gem を pkg に移動
  mv "dxruby64-#{GEM_VERSION}.gem", GEM_FILE
end

# --- Gemのインストール ---
desc "Install the gem (requires gem to be built)"
task :install do
  puts "Ruby Version: #{`ruby -v`}"
  unless File.exist?(GEM_FILE)
    abort "Gem file not found: #{GEM_FILE}. Please run 'rake build' first."
  end
  sh "gem install #{GEM_FILE}"
end

# --- RubyGems.orgに公開 ---
desc "Push the gem to RubyGems.org (requires gem to be built)"
task :release do
  unless File.exist?(GEM_FILE)
    abort "Gem file not found: #{GEM_FILE}. Please run 'rake build' first."
  end

  sh "gem push #{GEM_FILE}"
end

# --- Clean up ---
desc "Clean up build artifacts"
task :clean do
  rm_f GEM_FILE
end

