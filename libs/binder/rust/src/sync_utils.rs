/// A macro that allows working with std and spin Mutexes
/// when you're ok panicking on a poisoned Mutex.
#[cfg(all(feature = "std", not(single_threaded)))]
#[macro_export]
macro_rules! panic_if_poisoned {
    ( $poisonable:expr ) => {
        $poisonable.unwrap()
    };
}

/// A macro that allows working with std and spin Mutexes
/// when you're ok panicking on a poisoned Mutex.
#[cfg(single_threaded)]
#[macro_export]
macro_rules! panic_if_poisoned {
    ( $unpoisonable:expr ) => {
        // spin::Mutex does not support poisoning and returns
        // a MutexGuard directly
        $unpoisonable
    };
}
