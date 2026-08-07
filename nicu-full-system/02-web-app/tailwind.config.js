/** @type {import('tailwindcss').Config} */
export default {
  content: ['./index.html', './src/**/*.{js,jsx}'],
  theme: {
    extend: {
      colors: {
        bg: '#EAF0DA',        // soft sage/mint background
        card: '#FFFFFF',
        ink: '#212A1E',        // primary text (near-black green)
        muted: '#7C8874',      // secondary text
        lime: {
          DEFAULT: '#D7F24E',  // accent buttons/pills
          dark: '#B9D93A',
        },
        sage: {
          100: '#F1F5E7',
          200: '#E3EAD1',
          300: '#CBD8AC',
        },
        tan: '#F3E1BE',        // sleep-card style accent
        alert: '#E8604C',
        warn: '#F0B94D',
        good: '#6FAE5C',
      },
      fontFamily: {
        display: ['"Poppins"', 'sans-serif'],
        body: ['"Inter"', 'sans-serif'],
      },
      borderRadius: {
        '2xl': '1.25rem',
        '3xl': '1.75rem',
        '4xl': '2.25rem',
      },
      boxShadow: {
        soft: '0 8px 24px -8px rgba(33, 42, 30, 0.12)',
      },
    },
  },
  plugins: [],
};
