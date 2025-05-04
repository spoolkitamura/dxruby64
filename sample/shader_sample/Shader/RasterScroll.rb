# -*- coding: Windows-31J -*-

class RasterScrollShader < DXRuby::Shader
hlsl = <<EOS
float g_start;
float g_level;
texture tex0;
sampler Samp = sampler_state
{
 Texture =<tex0>;
 AddressU = CLAMP;
 AddressV = CLAMP;
};

struct PixelIn
{
  float2 UV : TEXCOORD0;
};
struct PixelOut
{
  float4 Color : COLOR0;
};

PixelOut PS(PixelIn input)
{
  PixelOut output;
  input.UV.x = input.UV.x + sin(radians(input.UV.y*360-g_start))*g_level;
  output.Color = tex2D( Samp, input.UV );

  return output;
}

technique Raster
{
 pass P0
 {
  PixelShader = compile ps_2_0 PS();
 }
}
EOS

  @@core = DXRuby::Shader::Core.new(
    hlsl,
    {
      :g_start => :float,
      :g_level => :float,
    }
  )

  attr_accessor :speed, :level

  # speed��1�t���[���ɐi�ފp�x(360�x�n)�Alevel�͐U���̑傫��(1.0��Image�T�C�Y)
  def initialize(speed=5, level=0.1)
    super(@@core, "Raster")
    self.g_start = 0.0
    self.g_level = 0.0
    @speed = speed
    @level = level

    # �V�F�[�_�p�����[�^�ݒ�̃��\�b�h�͊O������J�Ƃ���
    class << self
      protected :g_start, :g_start=, :g_level, :g_level=
    end
  end

  def update
    self.g_start += @speed
    self.g_level = @level
  end
end

