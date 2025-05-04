#!ruby -Ks
require 'dxruby'

# �����_�����O�ς݃t�H���g�����N���X
class ImageFontMaker
  ImageFontData = Struct.new(:x, :y, :width, :ox)
  ImageFontSaveData = Struct.new(:data_hash, :image, :height)

  def initialize(size, fontname = nil)
    @font = Font.new(size, fontname)
    @hash = {}
  end

  def add_data(str)
    str.each_char do |c|
      @hash[c] = @font.info(c)
    end
  end

  def output(filename)
    # �K�v�ȉ摜�T�C�Y�𒲂ׂ�
    # �v�Z����₱�������ƂɂȂ��Ă�̂�f��j�ȂǍ��E�ɂ͂ݏo������ȕ����̑Ή�
    width = 0
    height = 0
    keys = @hash.keys.shuffle
    keys.each do |k|
      v = @hash[k]
      ox = v.gmpt_glyphorigin_x < 0 ? -v.gmpt_glyphorigin_x : 0
      cx = (v.gm_blackbox_x + v.gmpt_glyphorigin_x) > v.gm_cellinc_x + ox ? (v.gm_blackbox_x + v.gmpt_glyphorigin_x) : v.gm_cellinc_x + ox
      width += cx
      if width > 640
        height += @font.size
        width = cx
      end
    end
    height += @font.size

    # �摜����
    image = Image.new(640, height, C_BLACK)

    # �����`������
    x = 0
    y = 0
    data_hash = {}
    keys.each do |k|
      v = @hash[k]
      ox = v.gmpt_glyphorigin_x < 0 ? -v.gmpt_glyphorigin_x : 0
      cx = (v.gm_blackbox_x + v.gmpt_glyphorigin_x) > v.gm_cellinc_x + ox ? (v.gm_blackbox_x + v.gmpt_glyphorigin_x) : v.gm_cellinc_x + ox
      if x + cx > 640
        x = 0
        y += @font.size
      end
      image.draw_font(x + ox, y, k, @font)
      data_hash[k] = ImageFontData.new(x, y, cx, ox)
      x += cx
    end

    # ��������ۑ�
    image.save(filename + ".png")

    # �o�C�i���œǂݍ���
    image_binary = nil
    open(filename + ".png", "rb") do |fh|
      image_binary = fh.read
    end

    # �}�[�V�������ăt�@�C����
    imagefont = ImageFontSaveData.new(data_hash, image_binary, @font.size)

    open(filename, "wb") do |fh|
      fh.write(Marshal.dump(imagefont))
    end
  end
end

# �����_�����O�ς݃t�H���g���g���N���X
class ImageFont
  attr_accessor :target

  # �������̃p�����[�^��draw_font_ex�̃n�b�V�������Ɠ���
  def initialize(filename)
    open(filename, "rb") do |fh|
      temp = Marshal.load(fh.read)
      @data_hash = temp.data_hash
      @image = Image.load_from_file_in_memory(temp.image)
      @height = temp.height
    end
    @cache = {}
  end

  # ������`��
  def draw(x, y, s, hash)
    rt = @target == nil ? Window : @target
    edge_width = hash[:edge_width] == nil ? 2 : hash[:edge_width]

    image_hash = nil
    if !@cache.has_key?(hash)
      image_hash = @cache[hash] = {}
    else
      image_hash = @cache[hash]
    end

    s.each_char do |c|
      temp = @data_hash[c]

      if !image_hash.has_key?(c)
        image = @image.slice(temp.x, temp.y, temp.width, @height)
        image_hash[c] = image.effect_image_font(hash)
        image.delayed_dispose
      end
      rt.draw(x - temp.ox - edge_width, y - edge_width, image_hash[c])
      x += temp.width - temp.ox
    end
  end
end

Window.bgcolor = [100,100,100]

# �����_�����O�ς݃t�H���g�쐬
imagefontmaker = ImageFontMaker.new(48, "�l�r �o�S�V�b�N")
imagefontmaker.add_data(" ABCDEFGHIJKLMNOPQRSTUVWXYZabcdef��ghijklmnopqrstuvwxyz�����������������������������������������������������ĂƂ����ÂłǂȂɂʂ˂̂͂Ђӂւق΂тԂׂڂς҂Ղ؂ۂ܂݂ނ߂�����������������������������A�B")
imagefontmaker.output("ImageFont.dat")

# �����_�����O�ς݃t�H���g�ǂݍ���
imagefont = ImageFont.new("ImageFont.dat")

Window.loop do
  imagefont.draw(50, 60, "���傤�͂��������邾�����B", :shadow=>true, :edge=>true, :edge_color=>C_CYAN)
  imagefont.draw(50, 108, "f��������������������", :shadow=>false, :edge=>true, :edge_color=>C_CYAN)
  imagefont.draw(50, 156, "j���������������Ă�", :shadow=>true, :edge=>false, :edge_color=>C_CYAN)
  imagefont.draw(50, 204, "k�Ȃɂʂ˂̂͂Ђӂւ�", :shadow=>true, :edge=>true, :edge_color=>C_RED)
end




