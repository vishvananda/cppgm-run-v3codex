static_assert(-(-2147483647 - 1) == -2147483648,
              "signed unary negation overflow is not a constant expression");
