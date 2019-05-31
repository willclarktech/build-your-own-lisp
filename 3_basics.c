#include <math.h>
#include <stdio.h>

char last_initial = 'H';
int age = 23;
long age_of_universe = 13798000000;
float litres_per_pint = 0.568f;
double speed_of_swallow = 0.01072896;

int add_together(int x, int y)
{
	int result = x + y;
	return result;
}

typedef struct
{
	float x;
	float y;
} point;

void greet_n(int n)
{
	for (int i = 0; i < n; i++)
	{
		puts("Hello World!");
	}
}

int main(int argc, char **argv)
{
	int added = add_together(10, 18);

	point p;
	p.x = 0.1;
	p.y = 10.0;
	float length = sqrt(p.x * p.x + p.y * p.y);

	if (length > 10 && length < 100)
	{
		puts("length is greater than 10 and less than 100!");
	}
	else
	{
		puts("x is less than 11 or greater than 99 :/");
	}

	int i = 10;
	while (i > 0)
	{
		puts("Loop iteration");
		i = i - 1;
	}

	for (int j = 0; j < 10; j++)
	{
		puts("Loop iteration 2");
	}

	greet_n(5);

	return added;
}
