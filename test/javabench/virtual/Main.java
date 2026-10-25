// Virtual calls down an abstract class hierarchy.
//
// Three levels of classes with overriding methods and super calls: a virtual
// call is a load of the vtable from the object and of the method from the
// vtable, then call_ref, unless TeaVM or the engine can prove the target.
public class Main {
    abstract static class Account {
        long balance;

        abstract long fee(long amount);

        long deposit(long amount) {
            balance += amount - fee(amount);
            return balance;
        }
    }

    static class Basic extends Account {
        long fee(long amount) {
            return amount / 100 + 1;
        }
    }

    static class Premium extends Basic {
        @Override
        long fee(long amount) {
            return super.fee(amount) / 2;
        }
    }

    static class Vip extends Premium {
        @Override
        long fee(long amount) {
            return amount > 1000 ? 0 : super.fee(amount);
        }

        @Override
        long deposit(long amount) {
            return super.deposit(amount + 1);
        }
    }

    public static void main(String[] args) {
        Account[] as = new Account[300];
        for (int i = 0; i < as.length; i++) {
            as[i] = switch (i % 3) {
                case 0 -> new Basic();
                case 1 -> new Premium();
                default -> new Vip();
            };
        }
        long total = 0;
        for (int round = 0; round < 40000; round++) {
            for (int i = 0; i < as.length; i++) {
                total += as[i].deposit((round * 7 + i) % 2000) & 0xffff;
            }
        }
        System.out.println("total = " + total);
    }
}
