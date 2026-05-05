for t in tests/*.tc; do
  echo "=== $t ==="
  valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
    --error-exitcode=1 ./build/main "$t" > /dev/null
done