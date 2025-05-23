
defmodule Factorization do
  def factorize_ordinary(n) do
    start_time = System.monotonic_time(:microsecond)
    result = factorize_ordinary(n, 2, [])
    end_time = System.monotonic_time(:microsecond)
    elapsed_time = end_time - start_time
    IO.puts("Ordinary Execution time: #{elapsed_time} µs")
    result
  end

  def factorize_ordinary(n, divisior, factors) do
    cond do
      n < 2 -> factors
      rem(n, divisior) == 0 -> factorize_ordinary(div(n, divisior), divisior, [divisior | factors])
      true -> factorize_ordinary(n, divisior + 1, factors)
    end
  end

  def factorize_single(n) do
    start_time = System.monotonic_time(:microsecond)
    max_divisior = :math.sqrt(n) |> trunc()
    numbers = Enum.to_list(2..max_divisior)
    result = factorize_single(n, numbers, [])
    end_time = System.monotonic_time(:microsecond)
    elapsed_time = end_time - start_time
    IO.puts("Single Execution time: #{elapsed_time} µs")
    result
  end
  defp factorize_single(1, _divisiors, factors), do: Enum.reverse(factors)
  defp factorize_single(n, [], factors), do: Enum.reverse([n | factors])

  defp factorize_single(n, [p | rest], factors) do
    cond do
      rem(n, p) == 0 ->
        factorize_single(div(n, p), [p | rest], [p | factors])
      true ->
        filtered_list = Enum.reject(rest, &(rem(&1, p) == 0))
        factorize_single(n, filtered_list, factors)
    end
  end

  def factorize_multi(n) do
    start_time = System.monotonic_time(:microsecond)
    max_divisor = :math.sqrt(n) |> trunc()
    numbers = Enum.to_list(2..max_divisor)

    # 재귀로 반복하면서 numbers에서 head를 꺼내고 numbers를 수정하고
    # 프로세스 만들어서 head로 나누어 떨어지면 인수로 추가하고
    result = factorize_multi(n, numbers, [])
    end_time = System.monotonic_time(:microsecond)
    elapsed_time = end_time - start_time
    IO.puts("Multi Execution time: #{elapsed_time} µs")
    result
  end

  defp factorize_multi(1, _divisiors, factors), do: factors
  defp factorize_multi(n, [], factors) when n != 1, do: factors ++ [n]

  defp factorize_multi(n, [p | rest], factors) do
    # 프로세스 만들어서 p로 나누어 떨어지면 인수로 p 추가 한 리스트 리턴
    parent = self()

    spawn(fn  ->
      result = factorize_recursive(n, p, [])
      send(parent, {:result, div(n, Enum.reduce(result, 1, &(&1 * &2))), result})
     end)

    filtered_list = Enum.reject(rest, &(rem(&1, p) == 0))
    receive do
      {:result, new_n, result} ->
        factorize_multi(new_n, filtered_list, factors ++ result)
    end
  end

  defp factorize_recursive(n, p, factors) do
    if rem(n, p) == 0 do
      factorize_recursive(div(n, p), p, [p | factors])
    else
      factors
    end
  end

  def factorize_gpt(n) do
    start_time = System.monotonic_time(:microsecond)
    max_divisor = :math.sqrt(n) |> trunc()
    numbers = Enum.to_list(2..max_divisor)

    result = factorize_parallel(n, numbers, [])
    end_time = System.monotonic_time(:microsecond)
    elapsed_time = end_time - start_time
    IO.puts("Multi Execution time: #{elapsed_time} µs")
    result
  end

  defp factorize_parallel(n, [], factors) do
    if n > 1, do: [n | factors], else: factors
  end

  defp factorize_parallel(n, [p | rest], factors) do
    task =
      Task.async(fn ->
        factorize_div(n, p, [])
      end)

    {new_n, new_factors} = Task.await(task, :infinity)

    filtered_list = Enum.reject(rest, &(rem(&1, p) == 0))

    factorize_parallel(new_n, filtered_list, new_factors ++ factors)
  end

  defp factorize_div(n, p, factors) do
    if rem(n, p) == 0 do
      factorize_div(div(n, p), p, [p | factors])
    else
      {n, factors}
    end
  end

  # 소수를 이용한 소인수분해
  def factorize_new1(n) do
    start_time = System.monotonic_time(:microsecond)
    primes = :math.sqrt(n) |> trunc() |> prime_list()

    result = factorize_new1(n, primes, [])
    remain = div(n, Enum.reduce(result, 1, &(&1 * &2)))
    result = if remain != 1 do
      result ++ [remain]
    else
      result
    end
    end_time = System.monotonic_time(:microsecond)
    elapsed_time = end_time - start_time
    IO.puts("New1 Execution time: #{elapsed_time} µs")
    result
  end

  defp factorize_new1(_, [], _), do: []
  defp factorize_new1(n, [p | primes], factors) do
    parent = self()

    spawn(fn  ->
      result = factorize_recursive(n, p, [])
      send(parent, {:result, result})
     end)
    others = factorize_new1(n, primes, factors)
    receive do
      {:result, result} ->
        result ++ others
    end
  end

  def prime_list(n) when n < 2, do: []
  def prime_list(n) do
    prime_list(Enum.to_list(2..n), n)
  end

  defp prime_list([], _), do: []
  defp prime_list([h | t], max) do
    [h | prime_list(Enum.reject(t, fn x -> rem(x, h) == 0 end), max)]
  end

  # C++ 코드를 기반으로 해결
  def factorize_cpp(n) do
    start_time = System.monotonic_time(:microsecond)
    parent = self()
    max_divisor = :math.sqrt(n) |> trunc()

    receiver_pid = spawn(fn -> receive_loop(MapSet.new(), [], parent) end)
    result = worker(n, 2, max_divisor, receiver_pid)

    3..max_divisor//2
    |> Enum.each(fn x ->
      send(receiver_pid, {:maybe_task, n, x, max_divisor})
    end)

    send(receiver_pid, :all_tasks_sent)

    final_result = gather_results(n, result)

    end_time = System.monotonic_time(:microsecond)
    elapsed_time = end_time - start_time
    IO.puts("C++ Execution time: #{elapsed_time} µs")
    final_result
  end

  defp receive_loop(set, tasks, parent) do
    self_pid = self()
    receive do
      {:maybe_task, n, p, max_divisor} ->
        if MapSet.member?(set, p) do
          receive_loop(set, tasks, parent)
        else
          new_set = MapSet.put(set, p)
          task =
            Task.async(fn  ->
              worker(n, p, max_divisor, self_pid)
            end)
          receive_loop(new_set, [task | tasks], parent)
        end
      {:sieve, x} ->
        receive_loop(MapSet.put(set, x), tasks, parent)
      :all_tasks_sent ->
        results = tasks |> Enum.map(&Task.await/1)
        send(parent, {:final_results, List.flatten(results)})
      _ ->
        receive_loop(set, tasks, parent)
    end
  end

  defp gather_results(n, result) do
    receive do
      {:final_results, list} ->
        full = result ++ list
        remain = div(n, Enum.reduce(full, 1, &(&1 * &2)))
        if remain != 1, do: full ++ [remain], else: full
    end
  end

  defp worker(n, p, max_divisor, parent) do
    # 인수 판단 및 에라토스테네스의 채
    if (:math.pow(p, 2) <= n) do
      result = recursive_divide(n, p)
      Enum.each(p..max_divisor//p, fn x ->
        send(parent, {:sieve, x})
      end)
      result
    else
      []
    end
  end

  defp recursive_divide(n, p) when rem(n, p) == 0 do
    [p | recursive_divide(div(n, p), p)]
  end
  defp recursive_divide(_, _), do: []

  # defp is_prime(n) when n <= 1, do: false
  # defp is_prime(2), do: true
  # defp is_prime(n) when rem(n, 2) == 0, do: false
  # defp is_prime(n), do: check_divisors(n, 3)
  # defp check_divisors(n, i) when i * i > n, do: true
  # defp check_divisors(n, i) do
  #   if rem(n, i) == 0 do
  #     false
  #   else
  #     check_divisors(n, i + 2)
  #   end
  # end

  # Trial Division 방식
  def factorize_trial(n) do
    start_time = System.monotonic_time(:microsecond)

    result = recursive_trial(n)

    end_time = System.monotonic_time(:microsecond)
    elapsed_time = end_time - start_time
    IO.puts("Trial Division Execution time: #{elapsed_time} µs")
    result
  end

  defp recursive_trial(n) do
    max_divisor = :math.sqrt(n) |> trunc()
    2..max_divisor
    |> Enum.chunk_every(1000)
    |> Enum.map(&Task.async(fn  -> find_divisor(n, &1) end))
    |> Enum.flat_map(&Task.await(&1, 10_000))
    |> case do
      [] -> [n] # 소수인 경우
      [d | _] -> [d | recursive_trial(div(n, d))]
    end
  end

  defp find_divisor(n, candidates) do
    Enum.filter(candidates, fn x -> rem(n, x) == 0 end)
  end


end


"""
실행결과 주석문


iex(6)> Factorization.factorize_ordinary(120)
Ordinary Execution time: 0 µs
[5, 3, 2, 2, 2]
iex(7)> Factorization.factorize_single(120)
Single Execution time: 0 µs
[2, 2, 2, 3, 5]
iex(8)> Factorization.factorize_multi(120)
Multi Execution time: 102 µs
[2, 2, 2, 3, 5]
iex(9)> Factorization.factorize_gpt(120)
Multi Execution time: 103 µs
[5, 3, 2, 2, 2]

iex(10)> Factorization.factorize_ordinary(1213500)
Ordinary Execution time: 0 µs
[809, 5, 5, 5, 3, 2, 2]
iex(11)> Factorization.factorize_single(1213500)
Single Execution time: 103 µs
[2, 2, 3, 5, 5, 5, 809]
iex(12)> Factorization.factorize_multi(1213500)
Multi Execution time: 307 µs
[2, 2, 3, 5, 5, 5, 809]
iex(13)> Factorization.factorize_gpt(1213500)
Multi Execution time: 512 µs
[809, 5, 5, 5, 3, 2, 2]

iex(14)> Factorization.factorize_ordinary(52462044112)
Ordinary Execution time: 7363174 µs
[3278877757, 2, 2, 2, 2]
iex(15)> Factorization.factorize_single(52462044112)
Single Execution time: 2627380 µs
[2, 2, 2, 2, 3278877757]
iex(16)> Factorization.factorize_multi(52462044112)
Multi Execution time: 2644583 µs
[2, 2, 2, 2, 3278877757]
iex(17)> Factorization.factorize_gpt(52462044112)
Multi Execution time: 2583757 µs
[3278877757, 2, 2, 2, 2]


# C++ 코드 기반반
iex(1)> Factorization.factorize_cpp(120)
C++ Execution time: 3380 µs
[2, 2, 2, 5, 3]
iex(2)> Factorization.factorize_cpp(1213500)
C++ Execution time: 20172 µs
[2, 2, 809, 375, 125, 75, 25, 15, 5, 5, 5, 3, 0]
iex(3)> Factorization.factorize_cpp(52462044112)
... (시간 측정 불가)


# Trial Division
iex(1)> Factorization.factorize_trial(120)
Trial Division Execution time: 5223 µs
[2, 2, 2, 3, 5]
iex(2)> Factorization.factorize_trial(1213500)
Trial Division Execution time: 205 µs
[2, 2, 3, 5, 5, 5, 809]
iex(3)> Factorization.factorize_trial(52462044112)
Trial Division Execution time: 39116 µs
[2, 2, 2, 2, 3278877757]


#####################################

  아래는 노트북으로 성능을 측정한 것!

#####################################

C++ 코드 속도
Enter a number : 52462044112
Exec Time : 3927ms.  Result = 2, 2, 2, 2, 3278877757
Enter a number : 125435156554214
Exec Time : 2762ms.  Result = 2, 13, 71, 887, 76606207
Enter a number : 24547478845155684
(성능 측정 불가...)

Trial Division
iex(2)> Factorization.factorize_trial(52462044112)
Trial Division Execution time: 65639 µs
[2, 2, 2, 2, 3278877757]
iex(3)> Factorization.factorize_trial(125435156554214)
Trial Division Execution time: 2322944 µs
[2, 13, 71, 887, 76606207]
iex(4)> Factorization.factorize_trial(24547478845155684)
Trial Division Execution time: 42498663 µs
[2, 2, 3, 11, 783283, 237418339]

"""
