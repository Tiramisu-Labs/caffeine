NAME=caffeine
SRC=main.c caffeine.c server.c
SRCDIR=src
SRCS=$(addprefix $(SRCDIR)/, $(SRC))
CC=gcc
CDFLAGS=-Werror -Wall -Wextra -o3
OBJ=$(SRCS:.c=.o)

all: $(NAME)

$(NAME): $(OBJ)
	$(CC) -g $(OBJ) -o $(NAME)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ)

fclean: clean
	rm -f $(NAME)

re: fclean all