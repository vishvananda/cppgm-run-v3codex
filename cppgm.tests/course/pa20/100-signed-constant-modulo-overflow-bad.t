static_assert((-2147483647 - 1) % -1 == 0,
              "invalid signed remainder is not a constant expression");
