require 'dxruby'

sound = Sound.new("gameover.wav")   # gameover.wav読み込み
sound.loop_count = 0                # 効果音は繰り返さない(「.loop_count = -1|0」形式での設定)
sound.set_volume(255)               # 効果音の音量(最大)(「.set_volume(値)」形式での設定)

bgm = Sound.new("backg.mp3")        # backg.mp3読み込み
bgm.loop = true                     # BGMは繰り返す(「.loop = true|false」形式での設定)
bgm.play                            # BGB再生
bgm.volume = 200                    # BGMの音量(「.volume = 値」形式での設定」)

Window.loop do
  Window.draw_font(20,  20, "[SPACE]  BGM停止", Font.default)
  Window.draw_font(20,  80, "[↑]     BGM音量上げ", Font.default)
  Window.draw_font(20, 110, "[↓]     BGM音量下げ", Font.default)
  Window.draw_font(20, 140, "音量 : #{bgm.volume}", Font.default, color: [255, 255, 0])
  Window.draw_font(20, 200, "[Z]      効果音 再生", Font.default)
  Window.draw_font(20, 350, "[ESCAPE] 終了", Font.default)

  if Input.key_push?(K_Z)           # [Z]キーで効果音を再生
    sound.play
  end
  if Input.key_push?(K_SPACE)       # SPACEキーでBGM終了
    bgm.stop
  end
  if Input.key_push?(K_UP)          # [↑]キーでBGMの音量を上げる
    bgm.volume = bgm.volume + 5     #「.volume = 値」形式
  end
  if Input.key_push?(K_DOWN)        # [↓]キーでBGMの音量を下げる
    bgm.volume = bgm.volume - 5     #「.volume = 値」形式
  end

  break if Input.key_push?(K_ESCAPE)
end
