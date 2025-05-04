# -*- coding: Windows-31J -*-

class FlashShader < DXRuby::Shader
  hlsl = <<EOS
// (1) �O���[�o���ϐ�
  float3 g_color;
  float g_level;
  texture tex0;

// (2) �T���v��
  sampler Samp0 = sampler_state
  {
   Texture =<tex0>;
   AddressU=BORDER;
   AddressV=BORDER;
  };

// (3) ���o�͂̍\����
  struct PixelIn
  {
    float2 UV : TEXCOORD0;
  };
  struct PixelOut
  {
    float4 Color : COLOR0;
  };

// (4) �s�N�Z���V�F�[�_�̃v���O����
  PixelOut PS(PixelIn input)
  {
    PixelOut output;
    output.Color.rgb = tex2D( Samp0, input.UV ).rgb * g_level + g_color * (1.0-g_level);
    output.Color.a = tex2D( Samp0, input.UV ).a;

    return output;
  }

// (5) technique��`
  technique Flash
  {
   pass P0 // �p�X
   {
    PixelShader = compile ps_2_0 PS();
   }
  }
EOS

  @@core = DXRuby::Shader::Core.new(
    hlsl,
    {
      :g_color => :float,
      :g_level => :float,
    }
  )


  # color�̓t���b�V���F�Aduration�͑J�ڂɂ�����t���[����
  def initialize(duration, color = [255, 255, 255])
    super(@@core, "Flash")
    self.color = color
    self.g_level = 1.0
    @duration = duration
    @count = 0

    # �V�F�[�_�p�����[�^�ݒ�̃��\�b�h�͊O������J�Ƃ���
    class << self
      protected :g_color, :g_color=, :g_level, :g_level=
    end
  end

  def color=(c)
      raise DXRuby::DXRubyError, "�F�z�񂪕s���ł�" if c.size != 3
      self.g_color = c.map{|v| v.fdiv(255)}
  end

  def duration=(d)
    @duration = d
  end

  def start
    @count = 0
    self.g_level = 0.0
  end

  def next
    @count += 1
    if @count > @duration
      self.g_level = 1.0
    else
      self.g_level = @count.fdiv(@duration)
    end
  end
end
