using System;
using System.Collections.Generic;
using System.Linq;

namespace Help2Design
{
    // A geometric containment chain gives wheel-up/down a consistent larger/smaller meaning,
    // even when image regions overlap or several providers report identical bounds.
    public sealed class CandidatePicker
    {
        public List<Candidate> Levels = new List<Candidate>();
        public int Index { get; private set; }
        public Candidate Current { get { return Levels.Count == 0 ? null : Levels[Index]; } }
        private PixelPoint anchor;
        public void Reset() { Levels.Clear(); Index = 0; anchor = null; }
        public void Update(IEnumerable<Candidate> candidates, int x, int y)
        {
            var previous = Current;
            bool samePlace = anchor != null && Math.Abs(anchor.x - x) <= 5 && Math.Abs(anchor.y - y) <= 5;
            var available = candidates.Where(c => c.bounds != null && c.bounds.Area() > 0 && c.bounds.Contains(x, y))
                .OrderBy(c => c.bounds.Area()).ThenBy(c => c.target != null && c.target.source == "uia" ? 0 : 1).ToList();
            var chain = new List<Candidate>();
            foreach (var c in available) {
                if (chain.Count == 0) { chain.Add(c); continue; }
                var inner = chain[chain.Count - 1].bounds;
                if (Contains(c.bounds, inner) && c.bounds.Area() > inner.Area() && !Same(c.bounds, inner)) chain.Add(c);
            }
            Levels = chain; Index = 0;
            if (samePlace && previous != null) { int i = Levels.FindIndex(c => Same(c.bounds, previous.bounds)); if (i >= 0) Index = i; }
            if (!samePlace || anchor == null) anchor = new PixelPoint(x, y);
        }
        public Candidate Step(int direction)
        {
            if (Levels.Count > 0) Index = Math.Max(0, Math.Min(Levels.Count - 1, Index + Math.Sign(direction)));
            return Current;
        }
        private static bool Contains(PixelRect outer, PixelRect inner) { return outer.x1 <= inner.x1 && outer.y1 <= inner.y1 && outer.x2 >= inner.x2 && outer.y2 >= inner.y2; }
        private static bool Same(PixelRect a, PixelRect b) { return a.x1 == b.x1 && a.y1 == b.y1 && a.x2 == b.x2 && a.y2 == b.y2; }
    }
}
