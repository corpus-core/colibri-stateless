# Prover

## Cache

the cmake-option -DPROOFER_CACHE activates a cache for objects. There is an global and a ctx-cache. when using `c4_prover_cache_get` first the ctx-cache is checked and only if we don't have an entry there the global cache will be searched. But if we find something in the global cache we also create an entry in the ctx-cache. So the next time this is checked, the result will come from the ctx-cache. Why? because this way we can make sure it is threadsafe.

`c4_prover_cache_set` will always just put something in the ctx-cache and only when releasing the context this cache-entry will be copied into the global cache. Also only cache entries with a ttl != 0 will be stored globally. This way you can also create cache_entries which are only stored within the ctx.

## Threadsafe

The prover runs in a evenqueue, which is managed by libuv. While this means the application runs in a single thread and we don't have to think about synchronizing threads, there ius one exceptioN: work-intensive tasks. The eventqueue should only be used with small fast functions so it does not block, but sometimes there are some heavier tasks that should not be executed in the eventloop. In order to make them safe you need to make sure the gollow those rules:

1. if you know heavy computation is ahead, use `REQUEST_WORKER_THREAD(ctx)` to switch to aworker-thread. Try to make sure you have fetched all the data you need before.
2. You must access all cached entries before entering in the working thread. calling `c4_prover_cache_get` will put a cache_entry in the local cache and add a mark, so the cleaner deamon will not free this resource as long as you are using it. you can access only the ctx-cache, but not the global cache with  `c4_prover_cache_get` after entering the worker thread.
3. Never modify data from the cache, since they are shared!

