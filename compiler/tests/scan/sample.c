int gcd(int a, int b) {
    while (b != 0) {
        int t;
        t = b;
        b = a - a / b * b;
        a = t;
    }
    return a;
}

void main() {
    int x;
    int y;
    float pi;
    pi = 3.14;
    pi = .5;
    pi = 1e-3;
    pi = 12.3E+4;
    x = 0;
    y = 10;
    while (x <= y) {
        if (x == y) {
            print(x);
        } else {
            x += 1;
        }
        x++;
    }
    if (x > 0 && y >= 0 || !x) {
        return;
    }
}
