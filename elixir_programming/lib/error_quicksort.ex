defmodule ErrorQuicksort do
  def sort([]), do: []
  def sort([pivot|t]) do
    sort(for x <- t, x < pivot, do: x)
    ++ [pivot] ++
    sort(for x <- t, x >= pivot, do: x)  # 문제가 되는 코드
    # 공백이 문제였다 기존 코드는 3, 4, 6 줄에 sort 와 ()가 공백으로 떨어져 있었는데 이게 문제가 되었다
    # 이때 공백으로 인해 sort가 독립적인 표현식(값)으로 해석될 여지가 있음
    # 이로인해 sort (for...) 가 바로 평가되지 않음 <- 함수 호출을 할 수도 있는데 공백으로 인해 함수 호출이 제대로 이루어 지지 않음
    # ++ 연산잔는 좌우의 값이 모두 평가 할 수 있어야 실행 되는데 sort () 에 의해 무한루프에 빠짐
  end
end
