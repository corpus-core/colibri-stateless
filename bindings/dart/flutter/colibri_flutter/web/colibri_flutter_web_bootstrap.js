/**
 * Colibri Flutter – web bootstrap.
 * Loads Colibri WASM from CDN, then the bridge, then starts Flutter.
 * Use in your app's index.html body: one script tag, no manual WASM build.
 */
(async function () {
  const CDN_VERSION = '1.1.15';
  try {
    const m = await import('https://unpkg.com/@corpus-core/colibri-stateless@' + CDN_VERSION);
    window.Colibri = m.default;
  } catch (e) {
    console.error('Colibri Flutter: failed to load Colibri from CDN:', e);
    document.body.innerHTML = '<p style="padding:1rem;font-family:sans-serif;">Failed to load Colibri. Check network and try again.</p>';
    return;
  }
  function loadScript(src, onLoad) {
    const s = document.createElement('script');
    s.src = src;
    if (onLoad) s.onload = onLoad;
    document.head.appendChild(s);
  }
  const bridgeUrl = new URL('colibri_web_bridge.js', import.meta.url).href;
  loadScript(bridgeUrl, function () {
    const base = document.querySelector('base')?.getAttribute('href') || '/';
    const flutterSrc = (base.endsWith('/') ? base : base + '/') + 'flutter_bootstrap.js';
    const f = document.createElement('script');
    f.src = flutterSrc;
    f.async = true;
    document.body.appendChild(f);
  });
})();
