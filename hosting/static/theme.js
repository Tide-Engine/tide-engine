// Apply the website's saved/system theme before the first paint.
try {
    const saved = localStorage.getItem('theme');
    document.documentElement.classList.toggle('dark', saved === 'dark'
        || (saved === null && matchMedia('(prefers-color-scheme: dark)').matches));
} catch {}
