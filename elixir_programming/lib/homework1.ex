defmodule MyModule do
  # 숫자 하나를 입력 받아서 1부터 그 숫자까지의 소수의 개수를 출력하는 프로그램
  def count_prime_number(n) when n > 1 do
    2..n
    |> Enum.filter(&is_prime?/1)
    |> Enum.count()
  end

  defp is_prime?(n) when n < 2, do: false
  defp is_prime?(2), do: true
  defp is_prime?(n), do: Enum.all?(2..(max(2 , (:math.sqrt(n)) |> trunc)), fn x -> rem(n, x) != 0 end)

  # 숫자 n을 입력 받아서 n번째 소수를 출력하는 프로그램
  def nth_prime(n) do

  end

  # 숫자 하나를 입력 받아서. 피보나치 수열 중에 그 숫자와 가장 가까운 숫자를 출력하는 프로그램
  def close_pibo(n) do

  end

  # 숫자 n을 입력 받아서 n개의 약수를 갖는 가장 작은 수를 출력하는 프로그램
  # (추가 필요) 소인수 분해를 하라.
  def smallest_measure_num(n) do

  end

  # 숫자 n을 입력 받아서 n!의 모든 자리수를 더해서 출력하는 프로그램.
  def factorial_digit_sum(n) do

  end

  # 숫자 n을 입력 받아서 a+b+c=n이 되는 피타고라스 수를 모두 출력하는 프로그램
  def pitagoras_print(n) do

  end

  # 숫자 n을 입력 받아서 그 숫자를 2진수로 나타냈을 때 1의 개수를 출력하는 프로그램
  def binary_1_counter(n) do

  end

  # 텍스트 파일의 이름을 입력 받아서 가장 많이 사용된 단어 10개를 사용 빈도와 함께 출력
  def find_many_10_word(n) do

  end

end
