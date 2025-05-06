require 'dxruby'

sound = Sound.new("gameover.wav")   # gameover.wav読み込み

puts sound.disposed?

Window.loop do
  if Input.key_push?(K_Z)           # [Z]キーで効果音を再生
    sound.play
    break
  end
end

sleep 1
sound.dispose
puts sound.disposed?

