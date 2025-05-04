#!ruby -Ks
require 'dxruby'
require './shader/transition'

Window.width = 800
Window.height = 600

shader = []
# ���[���摜��640*480�A�w�i�摜��800*600�Ȃ̂Ń��[���摜���g�債�ăV�F�[�_�ɐݒ肷��
# ����ȃ��\�b�h�`�F�C�����ł��Ă��܂��Ƃ����\
# ���\�b�h�̖߂�l�͌��\���������Ȃ̂ŁA�ł��Ă����͂��Ȃ̂ɂł��Ȃ����\�b�h����������A����������
shader << TransitionShader.new(100,
                               RenderTarget.new(800,600).
                                            draw_scale(0, 0, Image.load("./rule/�E�Q����.png"), 800/640.0, 600/480.0, 0, 0).
                                            update.
                                            to_image,
                               20)

# �������摜�����̂܂ܐH�킹��Ɖ�ʑS�̂ɌJ��Ԃ��Ďg��
shader << TransitionShader.new(100,
                               Image.load("./rule/�`�F�b�J�[.png"),
                               20)
shader << TransitionShader.new(100,
                               Image.load("./rule/���u���C���h.png"),
                               20)


# �w�i�摜
bg = [Image.load('./bgimage/BG10a_80.jpg'),
      Image.load('./bgimage/BG13a_80.jpg'),
      Image.load('./bgimage/BG00a1_80.jpg')
     ]

count = 100
mode = 0
image_new = bg[0]
image_old = nil

# �X�^�[�g����new�̂ݕ\��
# Z����������new��old�ɂȂ��āAold����O�ɕ`�悳��A�g�����W�V�������������B
# new��3���̉摜�����ԂɁB
Window.loop do
  if count < 100
    count += 1
    shader[mode].frame_count = count
  end

  if Input.key_down?(K_Z) && count == 100
    count = 0
    image_old = image_new
    mode = mode == 2 ? 0 : mode + 1
    image_new = bg[mode]
    shader[mode].start
  end

  Window.draw(0,0,image_new)
  Window.draw_shader(0,0,image_old, shader[mode]) if count < 100

  break if Input.key_push?(K_ESCAPE)
end


