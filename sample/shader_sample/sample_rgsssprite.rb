#!ruby -Ks
require 'dxruby'
require './shader/rgsssprite'

Window.width = 800
Window.height = 600

s = RgssSprite.new
s.image = Image.load('./bgimage/BG10a_80.jpg')

Window.loop do

  # Z�Ńt���b�V��������
  s.flash if Input.key_push?(K_Z)

  # X�������Ă���ԃO���C�X�P�[���ɂȂ�
  if Input.key_down?(K_X)
    s.gray = 255
  else
    s.gray = 0
  end

  # C�������Ă���ԁA�ԐF�������Ȃ�
  if Input.key_down?(K_C)
    s.tone = [-255,0,0]
  else
    s.tone = nil
  end

  # V�������Ă���ԁA���u�����h����
  if Input.key_down?(K_V)
    s.color = [60,0,0,255]
  else
    s.color = nil
  end

  s.update
  s.draw
  break if Input.key_push?(K_ESCAPE)
end

