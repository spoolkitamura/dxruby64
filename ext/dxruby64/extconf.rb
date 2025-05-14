require "mkmf"

# Windows システムライブラリ（リンク対象）
SYSTEM_LIBRARIES = [
  "dxguid",
  "d3d9",
  ["d3dx9_40", "d3dx9"],  # fallback
  "dinput8",
  "dsound",
  "gdi32",
  "ole32",
  "user32",
  "kernel32",
  "comdlg32",
  "winmm",
  "uuid",
  "imm32",
  # Media Foundation 関連
  "mfplat",
  "mfreadwrite",
  "mf",
  "mfuuid",
]

# ヘッダーファイルの存在チェック
REQUIRED_HEADERS = [
  "d3dx9.h",
  "dinput.h",
  "mfapi.h",         # Media Foundation API
  "mfidl.h",         # Media Foundation Interfaces
  "mfobjects.h",     # 基本型
  "mfreadwrite.h",   # SourceReader など
  "shlwapi.h",       # 一部 Windows API に必要
]

# Ruby の関数
REQUIRED_FUNCTIONS = [
  "rb_enc_str_new"
]

# ライブラリチェック（いずれか見つかれば OK なものにも対応）
SYSTEM_LIBRARIES.each do |libs|
  [*libs].any? { |lib| have_library(lib) }
end

# ヘッダーチェック
REQUIRED_HEADERS.each do |header|
  have_header(header)
end

# Ruby関数チェック
REQUIRED_FUNCTIONS.each do |func|
  have_func(func)
end

# Makefile生成
create_makefile("dxruby")

