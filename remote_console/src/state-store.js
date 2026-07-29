export class StateStore {
  #listeners = new Set();
  #snapshot;

  constructor(rootTopic) {
    this.rootTopic = rootTopic;
    this.#snapshot = {
      brokerConnected: false,
      availability: "unknown",
      meta: null,
      state: null,
      run: null,
      latest: null,
      plans: {},
      lastResult: null,
      receivedAt: {},
    };
  }

  get snapshot() {
    return structuredClone(this.#snapshot);
  }

  setBrokerConnected(connected) {
    this.#snapshot.brokerConnected = connected;
    this.#emit();
  }

  update(relativeTopic, value) {
    const now = new Date().toISOString();
    this.#snapshot.receivedAt[relativeTopic] = now;
    if (relativeTopic === "availability") {
      this.#snapshot.availability = value;
    } else if (relativeTopic === "meta") {
      this.#snapshot.meta = value;
    } else if (relativeTopic === "state") {
      this.#snapshot.state = value;
    } else if (relativeTopic === "run") {
      this.#snapshot.run = value;
    } else if (relativeTopic === "latest") {
      this.#snapshot.latest = value;
    } else if (relativeTopic === "result") {
      this.#snapshot.lastResult = value;
    } else {
      const match = /^plan\/([1-8])$/.exec(relativeTopic);
      if (match) this.#snapshot.plans[match[1]] = value;
    }
    this.#emit();
  }

  subscribe(listener) {
    this.#listeners.add(listener);
    return () => this.#listeners.delete(listener);
  }

  #emit() {
    const snapshot = this.snapshot;
    for (const listener of this.#listeners) listener(snapshot);
  }
}
