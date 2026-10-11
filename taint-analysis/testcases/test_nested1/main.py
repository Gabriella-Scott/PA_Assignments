def main(x):
    def inner(z):
        sink(z)
    inner(x)