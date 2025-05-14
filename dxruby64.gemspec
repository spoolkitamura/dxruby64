Gem::Specification.new do |spec|
  spec.name          = "dxruby64"
  spec.version       = "1.4.7.2"
  spec.authors       = ["Koki Kitamura"]
  spec.email         = ["spool.kitamura@nifty.ne.jp"]

  spec.summary       = "Custom build of DXRuby for 64-bit Ruby 3.1 and later (UCRT)"
  spec.description   = "This gem provides a custom build of DXRuby for Ruby 3.1 and later, compiled for 64-bit Ruby versions using UCRT (Universal C Runtime) on Windows. It is a personal, non-official version, not maintained by the original project."

  spec.files         = Dir["lib/**/*", "sample/**/*"] + ["README.md", "README-ja.md", "LICENSE.txt"]

  spec.require_paths = ["lib"]
  spec.required_ruby_version = ">= 3.1.0"

  spec.license       = "Zlib"
  spec.homepage      = "https://github.com/spoolkitamura/dxruby64"
  # spec.metadata["source_code_uri"] = spec.homepage
  spec.metadata["homepage_uri"]    = spec.homepage
end
