const CACHE_NAME = 'anyble-wifi-setup-v1';
const ASSETS_TO_CACHE = [
  '/',
  '/index.html',
  '/settings.html',
  '/lib/manifest.json',
  '/lib/offline.html',
  '/lib/babel.min.js',
  '/lib/react-dom.production.min.js',
  '/lib/react.production.min.js'
];

// Install event - cache assets
self.addEventListener('install', event => {
  event.waitUntil(
    caches.open(CACHE_NAME)
      .then(cache => {
        return cache.addAll(ASSETS_TO_CACHE);
      })
      .then(() => self.skipWaiting())
  );
});

// Activate event - clean up old caches
self.addEventListener('activate', event => {
  event.waitUntil(
    caches.keys().then(cacheNames => {
      return Promise.all(
        cacheNames.filter(cacheName => {
          return cacheName !== CACHE_NAME;
        }).map(cacheName => {
          return caches.delete(cacheName);
        })
      );
    }).then(() => self.clients.claim())
  );
});

// Fetch event - serve from cache, fallback to network
self.addEventListener('fetch', event => {
  // Skip WebSocket requests
  if (event.request.url.includes('/ws')) {
    return;
  }

  event.respondWith(
    caches.match(event.request)
      .then(response => {
        // Return cached response if found
        if (response) {
          return response;
        }

        // Clone the request because it's a one-time use stream
        const fetchRequest = event.request.clone();

        return fetch(fetchRequest).then(response => {
          // Check if valid response
          if (!response || response.status !== 200 || response.type !== 'basic') {
            return response;
          }

          // Clone the response because it's a one-time use stream
          const responseToCache = response.clone();

          caches.open(CACHE_NAME)
            .then(cache => {
              cache.put(event.request, responseToCache);
            });

          return response;
        }).catch(() => {
          // If fetch fails (offline), return a custom offline page or fallback
          // For API requests, we might want to return a custom response
          if (event.request.headers.get('accept').includes('application/json')) {
            return new Response(JSON.stringify({ 
              offline: true, 
              message: 'You are currently offline. Please reconnect to continue.' 
            }), {
              headers: { 'Content-Type': 'application/json' }
            });
          }
          
          // For HTML pages, return the offline page
          if (event.request.headers.get('accept').includes('text/html')) {
            return caches.match('/lib/offline.html');
          }
        });
      })
  );
});
