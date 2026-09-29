// Stand-in for ZXMAK2.Dependency's Locator (a Unity container configured from unity.config).
// The headless runner registers what the engine resolves (the PSG chip, message and question services);
// the "View" container is empty, so no window is ever created.
using System;
using System.Collections.Generic;

namespace ZXMAK2.Dependency
{
    public static class Locator
    {
        private static readonly HeadlessResolver _instance = new HeadlessResolver();

        // register a factory for T: called on every Resolve (unity.config registers these as transient)
        public static void Register<T>(Func<T> factory) { _instance.RegisterFactory(factory); }

        public static void Shutdown() { }
        public static T Resolve<T>() { return _instance.Resolve<T>(); }
        public static T Resolve<T>(string name) { return _instance.Resolve<T>(name); }
        public static T Resolve<T>(params Argument[] args) { return _instance.Resolve<T>(args); }
        public static T TryResolve<T>() { return _instance.TryResolve<T>(); }
        public static T TryResolve<T>(string name) { return _instance.TryResolve<T>(name); }
        public static T TryResolve<T>(params Argument[] args) { return _instance.TryResolve<T>(args); }
    }

    public sealed class HeadlessResolver : IResolver
    {
        private readonly Dictionary<string, Func<object>> _factories = new Dictionary<string, Func<object>>();

        private static string Key(Type type, string name) { return type.FullName + "|" + (name ?? string.Empty); }

        private object Create(Type type, string name)
        {
            Func<object> f;
            if (_factories.TryGetValue(Key(type, name), out f))
                return f();
            if (type == typeof(IResolver))
                return name == "View" ? new HeadlessResolver() : this;
            return null;
        }

        public void Dispose() { }

        public T Resolve<T>(params Argument[] args) { return Resolve<T>(null, args); }

        public T Resolve<T>(string name, params Argument[] args)
        {
            var o = Create(typeof(T), name);
            if (o == null)
                throw new InvalidOperationException("headless: nothing registered for " + typeof(T).FullName);
            return (T)o;
        }

        public T TryResolve<T>(params Argument[] args) { return TryResolve<T>(null, args); }

        public T TryResolve<T>(string name, params Argument[] args)
        {
            var o = Create(typeof(T), name);
            return o == null ? default(T) : (T)o;
        }

        public bool CheckAvailable<T>(params Argument[] args) { return _factories.ContainsKey(Key(typeof(T), null)); }
        public bool CheckAvailable<T>(string name, params Argument[] args) { return _factories.ContainsKey(Key(typeof(T), name)); }

        public void RegisterInstance<T>(string name, T instance) { _factories[Key(typeof(T), name)] = () => instance; }

        public void RegisterFactory<T>(Func<T> factory) { _factories[Key(typeof(T), null)] = () => factory(); }
    }
}
