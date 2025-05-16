document.getElementById('login-form').addEventListener('submit', function(event) {
    event.preventDefault(); // Prevent the form from submitting normally

    // Gather form data
    const username = document.getElementById('username').value;
    const password = document.getElementById('password').value;

    // Prepare JSON data
    const data = {
        username: username,
        password: password
    };

    // Send POST request
    fetch('/login', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
        },
        body: JSON.stringify(data)
    })
    .then(response => {
        if (response.redirected) {
            // If the server has sent a redirect, go to that location
            window.location.href = response.url;
        } else if (!response.ok) {
            throw new Error('Login failed');
        }
        return response.text(); // Or response.json() if the server sends JSON on success
    })
    .then(data => {
        // Handle any additional data sent by the server, if necessary
        console.log('Login successful:', data);
    })
    .catch((error) => {
        console.error('Error:', error);
        // Handle error, e.g., show an error message to the user
        alert('Login failed. Please try again.');
    });
});