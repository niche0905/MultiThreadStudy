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
  def nth_prime(n) when n > 1 do
    Stream.iterate(2, &(&1 + 1)) |>  Stream.filter(&is_prime?/1) |> Enum.take(n) |> List.last()
  end

  # 숫자 하나를 입력 받아서. 피보나치 수열 중에 그 숫자와 가장 가까운 숫자를 출력하는 프로그램
  def close_pibo(n) do
    Stream.unfold({0, 1}, fn {f1, f2} -> {f1, {f2, f1 + f2}} end)
    |> Enum.reduce_while({nil, nil}, fn fib, {prev, _ } ->
       if prev != nil and abs(prev - n) < abs(fib - n) do
         {:halt, prev} # 이전 값이 더 가까우므로 종료
       else
         {:cont, {fib, prev}} # 계속 진행
       end
     end)
  end

  # 숫자 n을 입력 받아서 n개의 약수를 갖는 가장 작은 수를 출력하는 프로그램
  # (추가 필요) 소인수 분해를 하라.
  def smallest_measure_num(n) do

  end

  defp factorize(1, _, factors), do: Enum.reverse(factors)
  defp factorize(n, divisor, factors) when rem(n, divisor) == 0 do
    factorize(div(n, divisor), divisor, [divisor | factors])
  end
  defp factorize(n, divisor, factors), do: factorize(n, next_prime(divisor), factors)

  defp next_prime(n) do
    Stream.iterate(n + 1, &(&1 + 1))
    |> Enum.find(&is_prime?/1)
  end

  # 숫자 n을 입력 받아서 n!의 모든 자리수를 더해서 출력하는 프로그램.
  def factorial_digit_sum(n) when n > 0  do
    num = 1..n
    |> Enum.reduce(fn a, b -> a * b end)
    digit_sum(num)
  end

  defp digit_sum(0), do: 0
  defp digit_sum(n), do: rem(n, 10) + digit_sum(div(n, 10))

  # 숫자 n을 입력 받아서 a+b+c=n이 되는 피타고라스 수를 모두 출력하는 프로그램
  def pitagoras_print(n) do
    nums = for a <- 1..(n-2),
        b <- 1..(n-2),
        c = n - a - b,
        c > 0,
        a * a + b * b == c * c or a * a + c * c == b * b or b * b + c * c == a * a,
        do: {a, b, c}
    IO.inspect(nums)
  end

  # 숫자 n을 입력 받아서 그 숫자를 2진수로 나타냈을 때 1의 개수를 출력하는 프로그램
  def binary_1_counter(n) when n >= 0 do
    binary_digit_sum(n)
  end

  defp binary_digit_sum(0), do: 0
  defp binary_digit_sum(n), do: rem(n, 2) + binary_digit_sum(div(n, 2))

  # 텍스트 파일의 이름을 입력 받아서 가장 많이 사용된 단어 10개를 사용 빈도와 함께 출력
  def find_many_10_word(file_name) do
    many_words = File.read!(file_name)
    |> String.split(~r/\s+/)
    |> Enum.frequencies()
    |> Enum.sort_by(fn {_word, count} -> -count end)  # 내림차순을 위해 -
    |> Enum.take(10)
    IO.inspect(many_words)
  end

end
