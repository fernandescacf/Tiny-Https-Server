document.addEventListener('DOMContentLoaded', () => {
    function fetchAndUpdatePortfolio() {
        fetch('/coins') // Assuming your server endpoint for fetching portfolio data is '/coins'
            .then(response => {
                if (!response.ok) {
                    throw new Error('Network response was not ok');
                }
                return response.json();
            })
            .then(portfolioData => {
                // Prepare the symbols for the Binance API call
                let symbols = portfolioData.map(coin => coin.symbol + 'USDT');
                return fetch(`https://api.binance.com/api/v3/ticker/24hr?symbols=["${symbols.join('","')}"]`)
                    .then(response => response.json())
                    .then(pricesData => {
                        let totalValue = 0;
                        let coinList = [];

                        portfolioData.forEach(coin => {
                            // Find the current price for the coin from Binance data
                            let priceData = pricesData.find(p => p.symbol === coin.symbol + 'USDT');
                            let currentPrice = priceData ? parseFloat(priceData.lastPrice) : 0;
                            
                            let value = coin.amount * currentPrice;
                            totalValue += value;
                            
                            // Calculate percentage change (you might need to fetch or calculate this from historical data)
                            let priceChangePercent = priceData ? parseFloat(priceData.priceChangePercent) : 0;
                            
                            // Calculate all-time profit
                            let allTimeProfit = ((currentPrice / coin.avgPrice) - 1) * 100;

                            coinList.push({
                                symbol: coin.symbol,
                                amount: parseFloat(coin.amount > 0.001 ? coin.amount.toFixed(3) : coin.amount.toFixed(8)),
                                currentPrice: currentPrice.toFixed(2),
                                priceChangePercent: priceChangePercent.toFixed(3),
                                value: value.toFixed(2),
                                allTimeProfit: allTimeProfit.toFixed(2),
                                allTimeProfit_usd: (coin.amount * currentPrice) - (coin.amount * coin.avgPrice)
                            });
                        });

                        // Sort coinList by value in descending order
                        coinList.sort((a, b) => parseFloat(b.value) - parseFloat(a.value));

                        updatePortfolioDisplay(totalValue, coinList);
                    });
            })
            .catch(error => console.error('Error fetching or processing data:', error));
    }

    // Initial fetch
    fetchAndUpdatePortfolio();

    // Set interval to update every second
    setInterval(fetchAndUpdatePortfolio, 1000);

    let allCoins = []; // This will store all coin symbols without USDT

    // Function to fetch USDT pairs and store them
    function fetchUSDTCoins() {
        fetch('https://api.binance.com/api/v3/ticker/price')
            .then(response => response.json())
            .then(data => {
                allCoins = data.filter(item => item.symbol.endsWith('USDT')).map(item => item.symbol.slice(0, -4));
                // No need to populate a select anymore, just store for autocomplete
            })
            .catch(error => console.error('Error fetching coin data:', error));
    }

    // Autocomplete functionality
    const coinInput = document.getElementById('coin-autocomplete');
    const suggestionsList = document.getElementById('coin-suggestions');

    coinInput.addEventListener('input', function() {
        const filter = this.value.toLowerCase();
        const filteredCoins = allCoins.filter(coin => coin.toLowerCase().includes(filter));

        suggestionsList.innerHTML = '';
        if (filter && filteredCoins.length > 0) {
            filteredCoins.forEach(coin => {
                let li = document.createElement('li');
                li.textContent = coin;
                li.addEventListener('click', () => {
                    coinInput.value = coin;
                    suggestionsList.innerHTML = ''; // Clear suggestions on selection
                });
                suggestionsList.appendChild(li);
            });
            // Position the suggestions list within the modal:
            // We use the modal content as the reference point, not the window
            const modalContent = document.querySelector('#add-transaction-modal .modal-content');
            const rect = this.getBoundingClientRect();
            const modalRect = modalContent.getBoundingClientRect();

            // Position relative to modal content
            suggestionsList.style.left = `${rect.left - modalRect.left}px`;
            suggestionsList.style.top = `${rect.bottom - modalRect.top}px`;
            suggestionsList.style.display = 'block';
        } else {
            suggestionsList.style.display = 'none';
        }
    });

    // Hide suggestions when clicking outside
    document.addEventListener('click', function(event) {
        if (!coinInput.contains(event.target) && !suggestionsList.contains(event.target)) {
            suggestionsList.style.display = 'none';
        }
    });

    // Event listener for the add transaction button
    document.getElementById('add-transaction').addEventListener('click', () => {
        openNewTransactionView("");
        const modal = document.getElementById('add-transaction-modal');
        modal.style.display = "block";
        fetchUSDTCoins(); // Fetch coins when modal opens
    });

    // Event listener for closing modal
    document.querySelectorAll('.close').forEach(closeBtn => {
        closeBtn.addEventListener('click', function() {
            this.closest('.modal').style.display = "none";
        });
    });

    // Event listener for form submission
    document.getElementById('add-transaction-form').addEventListener('submit', function(event) {
        event.preventDefault();

        // Collect form data
        const formData = new FormData(this);
        const data = Object.fromEntries(formData.entries());
    
        // Prepare data, including custom date formatting for sending
        const dateRaw = document.getElementById('date').value;
        const dateParts = dateRaw.split('-');
        const formattedDate = `${dateParts[2]}/${dateParts[1]}/${dateParts[0]}`;

        const requestData = {
            coin: document.getElementById('coin-autocomplete').value,
            amount: parseFloat(document.getElementById('amount').value),
            type: document.getElementById('type').value.charAt(0).toUpperCase() + document.getElementById('type').value.slice(1),
            value: parseFloat(document.getElementById('value').value),
            date: formattedDate
        };
    
        // Send POST request
        fetch('/coin', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
            },
            body: JSON.stringify(requestData)
        })
        .then(response => {
            if (!response.ok) {
                throw new Error('Network response was not ok');
            }
            return response.json(); // Assuming your server responds with JSON
        })
        .then(data => {
            console.log('Transaction added:', data);
            // Here you would handle the response from the server, 
            // perhaps updating your UI or showing a success message
            this.closest('.modal').style.display = "none"; // Close the modal
            // Optionally, you might want to refresh the transaction list or portfolio value here
        })
        .catch(error => {
            console.error('There was a problem with the fetch operation:', error);
            // Show an error message to the user
            alert('Failed to add transaction: ' + error.message);
        });
    });

});

function updatePortfolioDisplay(totalValue, coinList) {
    document.getElementById('total-value').textContent = totalValue.toFixed(2);
    let tbody = document.getElementById('coin-data');
    tbody.innerHTML = ''; // Clear existing data

    let total_profit_usd = document.getElementById('total-profit-loss');
    let profit_usd = 0;

    coinList.forEach(coin => {
        // First row for coin name, symbol, and icon
        let row1 = tbody.insertRow();
        let cell1_1 = row1.insertCell(0);

        profit_usd += coin.allTimeProfit_usd;
        
        // Set initial content with both image and text in a container for alignment
        cell1_1.innerHTML = `<div class="coin-entry"><img src="https://cdn.jsdelivr.net/gh/vadimmalykhin/binance-icons/crypto/${coin.symbol.toLowerCase()}.svg" class="coin-icon">${coin.symbol}</div>`;
        
        // Create a new Image object to test image availability
        let img = new Image();
        img.src = `https://cdn.jsdelivr.net/gh/vadimmalykhin/binance-icons/crypto/${coin.symbol.toLowerCase()}.svg`;
        
        // Check if image is available by loading it
        img.onerror = function() {
            // If image fails to load, hide the image
            cell1_1.innerHTML = `<div class="coin-entry"><img src="https://cdn.jsdelivr.net/gh/vadimmalykhin/binance-icons/crypto/${coin.symbol.toLowerCase()}.svg" style="visibility: hidden;" class="coin-icon">${coin.symbol}</div>`;
            //let imageElement = cell1_1.querySelector('.coin-icon');
            //imageElement.style.visibility = "hidden"
        };

        img.onload = function() {
            cell1_1.innerHTML = `<div class="coin-entry"><img src="https://cdn.jsdelivr.net/gh/vadimmalykhin/binance-icons/crypto/${coin.symbol.toLowerCase()}.svg" class="coin-icon">${coin.symbol}</div>`;
        }
        
        for (let i = 1; i < 4; i++) {
            let cell = row1.insertCell(i);
            switch(i) {
                case 1:
                    cell.innerHTML = `$${coin.currentPrice}<br><span class="percentage-change ${coin.priceChangePercent > 0 ? 'positive' : 'negative'}">${coin.priceChangePercent}%</span>`;
                    break;
                case 2:
                    cell.innerHTML = `${coin.amount}<br>$${coin.value}`;
                    break;
                case 3:
                    cell.innerHTML = `${coin.allTimeProfit}%<span class="profit-loss ${coin.allTimeProfit > 0 ? 'positive' : 'negative'}">$${coin.allTimeProfit_usd.toFixed(2)}</span>`;
                    break;
            }
        }

        // Function to fetch USDT pairs and store them
        function fetchUSDTCoins() {
            fetch('https://api.binance.com/api/v3/ticker/price')
                .then(response => response.json())
                .then(data => {
                    allCoins = data.filter(item => item.symbol.endsWith('USDT')).map(item => item.symbol.slice(0, -4));
                    // No need to populate a select anymore, just store for autocomplete
                })
                .catch(error => console.error('Error fetching coin data:', error));
        }
        
        cell = row1.insertCell(4);
        const button = document.createElement("add-transaction");
        button.textContent = "+";
        button.style.fontSize = "1.5em"
        button.onclick = () => {
            event.stopPropagation();
            openNewTransactionView(row1.cells[0].textContent);
            document.getElementById('add-transaction-modal').style.display = "block";
            fetchUSDTCoins();
        }
        cell.appendChild(button);

        // Make the entire coin entry clickable
        row1.addEventListener('click', function() {
            fetch(`/coin?${coin.symbol}`)
                .then(response => {
                    if (!response.ok) {
                        throw new Error('Network response was not ok');
                    }
                    return response.json();
                })
                .then(transactions => {
                    displayTransactions(coin.symbol, transactions);
                })
                .catch(error => console.error('Error fetching transactions:', error));
        });

        row1.style.cursor = 'pointer';
    });

    document.getElementById('percentage-change').textContent = parseFloat((((Math.abs(profit_usd + totalValue) / totalValue) - 1) * 100).toFixed(2)) + '%';
    total_profit_usd.textContent = '$' + parseFloat(profit_usd.toFixed(2));
}

function openNewTransactionView(coin) {
    document.getElementById('coin-autocomplete').value = coin;
    document.getElementById('amount').value = 0;
    document.getElementById('type').value = 'buy';
    document.getElementById('value').value = 0;
    let date = document.getElementById('date');
    date.value = new Date().toISOString().split("T")[0];
}

// Function to display transactions in the modal
function displayTransactions(symbol, transactions) {
    const modal = document.getElementById('transaction-modal');
    const closeBtn = document.querySelector('.close');
    const modalCoinSymbol = document.getElementById('modal-coin-symbol');
    const tbody = document.getElementById('transaction-data');

    modalCoinSymbol.textContent = symbol;
    tbody.innerHTML = ''; // Clear previous transactions

    transactions.forEach(transaction => {
        let row = tbody.insertRow();
        row.insertCell(0).textContent = transaction.coin;
        row.insertCell(1).textContent = transaction.amount;
        row.insertCell(2).textContent = transaction.value;
        row.insertCell(3).textContent = transaction.date;
    });

    modal.style.display = "block"; // Show the modal

    // When the user clicks on <span> (x), close the modal
    closeBtn.onclick = function() {
        modal.style.display = "none";
    }

    // When the user clicks anywhere outside of the modal, close it
    window.onclick = function(event) {
        if (event.target == modal) {
            modal.style.display = "none";
        }
    }
}