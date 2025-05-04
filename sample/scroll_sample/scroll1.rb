# �X�N���[���T���v�����̂P(�P�����[�v�X�N���[��)
require 'dxruby'
require './map'

# �G�̃f�[�^�����
mapimage = []
mapimage.push(Image.new(32, 32, [100, 100, 200])) # �C
mapimage.push(Image.new(32, 32, [50, 200, 50]))   # ���n
mapimage.push(Image.new(32, 32, [50, 200, 50]).   # �؂̍���
                        box_fill(13, 0, 18, 28, [200, 50, 50]))
mapimage.push(Image.new(32, 32, [50, 200, 50]).   # �R
                        triangle_fill(15, 0, 0, 31, 31, 31, [200, 100,100]))
mapimage.push(Image.new(32, 32).  # �؂̂����܁B�w�i�͓����F�ɂ��Ă����B
                        box_fill(13, 16, 18, 31, [200, 50, 50]).
                        circle_fill(16, 10, 8, [0, 255, 0]))

# Fiber���g���₷�����郂�W���[��
module FiberSprite
  def initialize(x=0,y=0,image=nil)
    super
    @fiber = Fiber.new do
      self.fiber_proc
    end
  end

  def update
    @fiber.resume
    super
  end

  def wait(t=1)
    t.times{Fiber.yield}
  end
end

# ���L����
class Player < Sprite
  include FiberSprite
  attr_accessor :mx, :my

  def initialize(x, y, map, target=Window)
    @mx, @my, @map, self.target = x, y, map, target
    super(8.5 * 32, 6 * 32)

    # ���͏�ɂ͂ݏo���ĕ`�悳���̂ł��̂Ԃ�ʒu�␳����׍H
    self.center_x = 0
    self.center_y = 16
    self.offset_sync = true

    # �_�l�ԉ摜
    self.image = Image.new(32, 48).
                       circle(15, 5, 5, [255, 255, 255]).
                       line(5, 18, 26, 18, [255, 255, 255]).
                       line(15, 10, 15, 31, [255, 255, 255]).
                       line(15, 31, 5, 47, [255, 255, 255]).
                       line(15, 31, 25, 47, [255, 255, 255])
  end

  # Player#update����ƌĂ΂��Fiber�̒��g
  def fiber_proc
    loop do
      ix, iy = Input.x, Input.y

      # �����ꂽ�`�F�b�N
      if ix + iy != 0 and (ix == 0 or iy == 0) and # �i�i���͋p��
         @map[@mx/32+ix, @my/32+iy] == 1   # �ړ��悪���n�̂Ƃ��̂�
        # 8�t���[����1�}�X�ړ�
        8.times do
          @mx += ix * 4
          @my += iy * 4
          wait # wait����Ǝ��̃t���[����
        end
      else
        wait
      end
    end
  end
end

# RenderTarget�쐬
rt = RenderTarget.new(640-64, 480-64)

# �}�b�v�̍쐬
map_base = Map.new("map.dat", mapimage, rt)
map_sub = Map.new("map_sub.dat", mapimage, rt)

# ���L����
player = Player.new(0, 0, map_base, rt)

Window.loop do
  # �l�ړ�����
  player.update

  # rt�Ƀx�[�X�}�b�v��`��
  map_base.draw(player.mx - player.x, player.my - player.y)

  # rt�ɐl�`��
  player.draw

  # rt�ɏ�w�}�b�v��`��
  map_sub.draw(player.mx - player.x, player.my - player.y)

  # rt����ʂɕ`��
  Window.draw(32, 32, rt)

  # �G�X�P�[�v�L�[�ŏI��
  break if Input.key_push?(K_ESCAPE)
end
