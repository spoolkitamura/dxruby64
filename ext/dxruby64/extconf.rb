require "mkmf"

# Windows �V�X�e�����C�u�����i�����N�Ώہj
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
  # Media Foundation �֘A
  "mfplat",
  "mfreadwrite",
  "mf",
  "mfuuid",
]

# �w�b�_�[�t�@�C���̑��݃`�F�b�N
REQUIRED_HEADERS = [
  "d3dx9.h",
  "dinput.h",
  "mfapi.h",         # Media Foundation API
  "mfidl.h",         # Media Foundation Interfaces
  "mfobjects.h",     # ��{�^
  "mfreadwrite.h",   # SourceReader �Ȃ�
  "shlwapi.h",       # �ꕔ Windows API �ɕK�v
]

# Ruby �̊֐�
REQUIRED_FUNCTIONS = [
  "rb_enc_str_new"
]

# ���C�u�����`�F�b�N�i�����ꂩ������� OK �Ȃ��̂ɂ��Ή��j
SYSTEM_LIBRARIES.each do |libs|
  [*libs].any? { |lib| have_library(lib) }
end

# �w�b�_�[�`�F�b�N
REQUIRED_HEADERS.each do |header|
  have_header(header)
end

# Ruby�֐��`�F�b�N
REQUIRED_FUNCTIONS.each do |func|
  have_func(func)
end

# Makefile����
create_makefile("dxruby")

