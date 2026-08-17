#pragma once

struct specialization_tag {};

template<class T>
int specialized_value(int);

template<>
int specialized_value<specialization_tag>(int);
